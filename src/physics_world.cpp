#include "physics_world.hpp"

#include "fps_camera.hpp"
#include "level_loader.hpp"

#include <bx/math.h>

#include <cmath>

namespace engine {

namespace {

bool sample_in_wall(const LoadedLevel& level, float wx, float wz)
{
	int c = 0;
	int r = 0;
	level.world_to_cell(wx, wz, c, r);
	return level.in_bounds(c, r) && level.is_wall(c, r);
}

bool body_hits_wall_xy(const LoadedLevel& level, float eye_x, float eye_z, float radius)
{
	if (sample_in_wall(level, eye_x, eye_z)) {
		return true;
	}
	static constexpr int k_samples = 8;
	for (int i = 0; i < k_samples; ++i) {
		const float a = (bx::kPi * 2.0f * static_cast<float>(i)) / static_cast<float>(k_samples);
		const float sx = eye_x + std::cos(a) * radius;
		const float sz = eye_z + std::sin(a) * radius;
		if (sample_in_wall(level, sx, sz)) {
			return true;
		}
	}
	return false;
}

void resolve_horizontal_wall_slide(
	const LoadedLevel& level,
	float prev_x,
	float prev_z,
	float cand_x,
	float cand_z,
	float radius,
	float& out_x,
	float& out_z
)
{
	if (!body_hits_wall_xy(level, cand_x, cand_z, radius)) {
		out_x = cand_x;
		out_z = cand_z;
		return;
	}
	if (!body_hits_wall_xy(level, cand_x, prev_z, radius)) {
		out_x = cand_x;
		out_z = prev_z;
		return;
	}
	if (!body_hits_wall_xy(level, prev_x, cand_z, radius)) {
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
	const LoadedLevel& level,
	float prev_eye_x,
	float prev_eye_z
)
{
	physics.velocity_y -= PlayerPhysics::k_gravity * dt;
	camera.eyeY += physics.velocity_y * dt;

	// Wall slide: full WASD delta can overlap wall samples while sliding along it; try X-only / Z-only.
	const float move_dx = camera.eyeX - prev_eye_x;
	const float move_dz = camera.eyeZ - prev_eye_z;
	const float cand_x = prev_eye_x + move_dx;
	const float cand_z = prev_eye_z + move_dz;
	resolve_horizontal_wall_slide(
		level,
		prev_eye_x,
		prev_eye_z,
		cand_x,
		cand_z,
		PlayerPhysics::k_body_radius_xz,
		camera.eyeX,
		camera.eyeZ
	);

	const float feet_y = camera.eyeY - PlayerPhysics::k_eye_height;
	const bool on_floor_cell = level.walkable_at_world(camera.eyeX, camera.eyeZ);

	if (feet_y <= k_platform_surface_y && on_floor_cell && physics.velocity_y <= 0.0f) {
		camera.eyeY = k_platform_surface_y + PlayerPhysics::k_eye_height;
		physics.velocity_y = 0.0f;
	}
}

} // namespace engine
