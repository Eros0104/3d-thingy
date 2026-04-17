#include "physics_world.hpp"

#include "fps_camera.hpp"
#include "level_data.hpp"

#include <cmath>
#include <limits>

namespace engine {

namespace {

constexpr float k_no_surface = -std::numeric_limits<float>::infinity();

struct SegProj {
	float t;           ///< Parameter along segment (0 at a, 1 at b), unclamped.
	float t_clamped;   ///< Clamped to [0, 1].
	float dist;        ///< Distance from point to closest-on-segment (clamped).
	float cx;          ///< Closest point X (clamped).
	float cz;          ///< Closest point Z (clamped).
};

SegProj project_to_segment(Vec2 a, Vec2 b, float px, float pz)
{
	SegProj r;
	const float dx = b.x - a.x;
	const float dz = b.z - a.z;
	const float len_sq = dx * dx + dz * dz;
	if (len_sq <= 1e-8f) {
		r.t = 0.0f;
		r.t_clamped = 0.0f;
		r.cx = a.x;
		r.cz = a.z;
	} else {
		r.t = ((px - a.x) * dx + (pz - a.z) * dz) / len_sq;
		r.t_clamped = r.t < 0.0f ? 0.0f : (r.t > 1.0f ? 1.0f : r.t);
		r.cx = a.x + dx * r.t_clamped;
		r.cz = a.z + dz * r.t_clamped;
	}
	const float ex = px - r.cx;
	const float ez = pz - r.cz;
	r.dist = std::sqrt(ex * ex + ez * ez);
	return r;
}

/// True if a Door wall lets the player pass through at the closest point. We check only the
/// horizontal door opening window along the segment; vertical fit is assumed (door_height is
/// always >= player eye height in typical mansions).
bool wall_is_passable(const Wall& w, const SegProj& proj, float feet_y)
{
	if (w.type != WallType::Door) {
		return false;
	}
	const float dx = w.b.x - w.a.x;
	const float dz = w.b.z - w.a.z;
	const float len = std::sqrt(dx * dx + dz * dz);
	if (len <= 1e-6f) {
		return false;
	}
	const float door_top = w.y0 + w.door_height;
	if (feet_y < w.y0 - 0.1f || feet_y + PlayerPhysics::k_eye_height > door_top + 0.2f) {
		// Feet below bottom of door or head above top of door: blocked.
		return false;
	}
	float door_off = w.door_offset;
	float door_w = w.door_width;
	if (door_w > len) door_w = len;
	if (door_off < 0.0f) door_off = 0.5f * (len - door_w);
	if (door_off < 0.0f) door_off = 0.0f;
	if (door_off + door_w > len) door_off = len - door_w;
	const float t0 = door_off / len;
	const float t1 = (door_off + door_w) / len;
	return proj.t_clamped > t0 && proj.t_clamped < t1;
}

/// True if the wall's vertical span overlaps the player's body (feet to eye). Walls whose span
/// is entirely above the player's head or below the player's feet don't collide horizontally.
bool wall_spans_body(const Wall& w, float feet_y)
{
	const float head_y = feet_y + PlayerPhysics::k_eye_height;
	return w.y1 > feet_y && w.y0 < head_y;
}

/// Walkable surface Y at (wx, wz) if the point is over a stair rectangle. Returns k_no_surface
/// otherwise. Y matches the discrete step tops emitted by the mesh builder (so feet snap onto
/// each step rather than sliding on a smooth ramp that would clip into the rendered stairs).
float stair_surface_y_at(const Stair& s, float wx, float wz)
{
	const float dx = s.center_b.x - s.center_a.x;
	const float dz = s.center_b.z - s.center_a.z;
	const float len_sq = dx * dx + dz * dz;
	if (len_sq <= 1e-8f) {
		return k_no_surface;
	}
	const float t = ((wx - s.center_a.x) * dx + (wz - s.center_a.z) * dz) / len_sq;
	if (t < 0.0f || t > 1.0f) {
		return k_no_surface;
	}
	const float cx = s.center_a.x + dx * t;
	const float cz = s.center_a.z + dz * t;
	const float ex = wx - cx;
	const float ez = wz - cz;
	const float perp_dist = std::sqrt(ex * ex + ez * ez);
	if (perp_dist > 0.5f * s.width) {
		return k_no_surface;
	}
	const int steps = s.steps < 1 ? 1 : s.steps;
	int step_idx = static_cast<int>(t * static_cast<float>(steps));
	if (step_idx >= steps) step_idx = steps - 1;
	const float top_t = static_cast<float>(step_idx + 1) / static_cast<float>(steps);
	return s.from_y + (s.to_y - s.from_y) * top_t;
}

/// The highest walkable surface at (wx, wz) whose top is <= max_y. Returns k_no_surface if none.
float highest_walkable_surface(const Level& level, float wx, float wz, float max_y)
{
	float best = k_no_surface;
	const Vec2 p{wx, wz};
	for (const Sector& s : level.sectors) {
		if (!point_in_polygon(s.polygon, p)) {
			continue;
		}
		if (s.floor_y <= max_y && s.floor_y > best) {
			best = s.floor_y;
		}
	}
	for (const Stair& s : level.stairs) {
		const float y = stair_surface_y_at(s, wx, wz);
		if (y == k_no_surface) {
			continue;
		}
		if (y <= max_y && y > best) {
			best = y;
		}
	}
	return best;
}

bool body_hits_any_wall(
	const Level& level,
	float px, float pz,
	float feet_y,
	float radius)
{
	for (const Wall& w : level.walls) {
		if (!wall_spans_body(w, feet_y)) {
			continue;
		}
		const SegProj proj = project_to_segment(w.a, w.b, px, pz);
		if (proj.dist >= radius) {
			continue;
		}
		if (wall_is_passable(w, proj, feet_y)) {
			continue;
		}
		return true;
	}
	return false;
}

/// True if (wx, wz) has walkable structure overhead (sector floor or stair) but none of those
/// surfaces is reachable from `feet_y` within `step_up`. Stairs and sectors count equally: if
/// ANY walkable surface at this point is reachable, movement is allowed (e.g. walking onto a
/// stair cell whose step is climbable even though the sector floor above is not). Empty void
/// (no sector, no stair) does not block — the player falls naturally.
bool cliff_blocks_climb(const Level& level, float wx, float wz, float feet_y, float step_up)
{
	const Vec2 p{wx, wz};
	bool any_walkable = false;
	bool any_reachable = false;
	const float max_y = feet_y + step_up;

	for (const Sector& s : level.sectors) {
		if (!point_in_polygon(s.polygon, p)) {
			continue;
		}
		any_walkable = true;
		if (s.floor_y <= max_y) {
			any_reachable = true;
			break;
		}
	}
	if (!any_reachable) {
		for (const Stair& s : level.stairs) {
			const float y = stair_surface_y_at(s, wx, wz);
			if (y == k_no_surface) {
				continue;
			}
			any_walkable = true;
			if (y <= max_y) {
				any_reachable = true;
				break;
			}
		}
	}
	return any_walkable && !any_reachable;
}

bool body_hits_wall_or_cliff(
	const Level& level,
	float px, float pz,
	float feet_y,
	float radius,
	float step_up)
{
	if (body_hits_any_wall(level, px, pz, feet_y, radius)) {
		return true;
	}
	if (cliff_blocks_climb(level, px, pz, feet_y, step_up)) {
		return true;
	}
	return false;
}

void resolve_horizontal_slide(
	const Level& level,
	float prev_x, float prev_z,
	float cand_x, float cand_z,
	float feet_y,
	float radius,
	float step_up,
	float& out_x, float& out_z)
{
	if (!body_hits_wall_or_cliff(level, cand_x, cand_z, feet_y, radius, step_up)) {
		out_x = cand_x;
		out_z = cand_z;
		return;
	}
	if (!body_hits_wall_or_cliff(level, cand_x, prev_z, feet_y, radius, step_up)) {
		out_x = cand_x;
		out_z = prev_z;
		return;
	}
	if (!body_hits_wall_or_cliff(level, prev_x, cand_z, feet_y, radius, step_up)) {
		out_x = prev_x;
		out_z = cand_z;
		return;
	}
	out_x = prev_x;
	out_z = prev_z;
}

} // namespace

void player_physics_step(
	FpsCamera& camera,
	PlayerPhysics& physics,
	float dt,
	const Level& level,
	float prev_eye_x,
	float prev_eye_z)
{
	physics.velocity_y -= PlayerPhysics::k_gravity * dt;
	camera.eyeY += physics.velocity_y * dt;

	const float feet_y = camera.eyeY - PlayerPhysics::k_eye_height;

	const float move_dx = camera.eyeX - prev_eye_x;
	const float move_dz = camera.eyeZ - prev_eye_z;
	const float cand_x = prev_eye_x + move_dx;
	const float cand_z = prev_eye_z + move_dz;
	resolve_horizontal_slide(
		level,
		prev_eye_x, prev_eye_z,
		cand_x, cand_z,
		feet_y,
		PlayerPhysics::k_body_radius_xz,
		PlayerPhysics::k_step_up,
		camera.eyeX, camera.eyeZ
	);

	const float new_feet_y = camera.eyeY - PlayerPhysics::k_eye_height;
	const float surface = highest_walkable_surface(
		level,
		camera.eyeX, camera.eyeZ,
		new_feet_y + PlayerPhysics::k_step_up
	);

	if (surface != k_no_surface && new_feet_y <= surface && physics.velocity_y <= 0.0f) {
		camera.eyeY = surface + PlayerPhysics::k_eye_height;
		physics.velocity_y = 0.0f;
	}
}

} // namespace engine
