#include "level_mesh.hpp"

#include <cmath>

namespace engine {

namespace {

constexpr float k_floor_uv_per_world = 1.0f / 6.0f;
constexpr float k_wall_uv_scale = 0.35f;
constexpr uint32_t k_floor_abgr = 0xffffffffu;
constexpr uint32_t k_wall_abgr = 0xffe8e4ddu;
constexpr uint32_t k_stair_abgr = 0xffd8d4ccu;

constexpr float k_broken_wall_height = 1.05f;  ///< Remnant height for `Broken` wall type.
constexpr float k_window_sill_h = 1.2f;        ///< World-Y distance from wall.y0 to sill bottom.
constexpr float k_window_top_h = 2.1f;         ///< World-Y distance from wall.y0 to slit top.

void push_v(
	std::vector<LitVertex>& v,
	float x, float y, float z,
	float nx, float ny, float nz,
	float u, float vv,
	uint32_t abgr)
{
	LitVertex vert;
	vert.x = x;
	vert.y = y;
	vert.z = z;
	vert.nx = nx;
	vert.ny = ny;
	vert.nz = nz;
	vert.u = u;
	vert.v = vv;
	vert.abgr = abgr;
	v.push_back(vert);
}

// ---------------- Sector floor triangulation (ear clipping) ---------------------------------

bool triangle_contains(Vec2 a, Vec2 b, Vec2 c, Vec2 p)
{
	const float s1 = (b.x - a.x) * (p.z - a.z) - (b.z - a.z) * (p.x - a.x);
	const float s2 = (c.x - b.x) * (p.z - b.z) - (c.z - b.z) * (p.x - b.x);
	const float s3 = (a.x - c.x) * (p.z - c.z) - (a.z - c.z) * (p.x - c.x);
	const bool has_neg = (s1 < 0) || (s2 < 0) || (s3 < 0);
	const bool has_pos = (s1 > 0) || (s2 > 0) || (s3 > 0);
	return !(has_neg && has_pos);
}

/// Signed cross (z-component) of (b-a) x (c-b); > 0 means CCW turn at b (convex for CCW polygon).
float cross_z(Vec2 a, Vec2 b, Vec2 c)
{
	return (b.x - a.x) * (c.z - b.z) - (b.z - a.z) * (c.x - b.x);
}

void emit_floor_tri(std::vector<LitVertex>& out, Vec2 a, Vec2 b, Vec2 c, float y)
{
	// Winding chosen so the resulting surface normal points +Y (top-facing floor).
	const auto add = [&](Vec2 p) {
		push_v(out, p.x, y, p.z, 0.0f, 1.0f, 0.0f,
			p.x * k_floor_uv_per_world, p.z * k_floor_uv_per_world, k_floor_abgr);
	};
	add(a); add(c); add(b);
}

void emit_ceiling_tri(std::vector<LitVertex>& out, Vec2 a, Vec2 b, Vec2 c, float y)
{
	// Opposite winding of floor so the surface normal points -Y (viewable from below).
	const auto add = [&](Vec2 p) {
		push_v(out, p.x, y, p.z, 0.0f, -1.0f, 0.0f,
			p.x * k_floor_uv_per_world, p.z * k_floor_uv_per_world, k_floor_abgr);
	};
	add(a); add(b); add(c);
}

} // namespace

bool triangulate_polygon(const std::vector<Vec2>& poly, std::vector<int>& out_indices)
{
	out_indices.clear();
	const int n = static_cast<int>(poly.size());
	if (n < 3) {
		return false;
	}
	if (n == 3) {
		out_indices = {0, 1, 2};
		return true;
	}
	std::vector<int> idx(static_cast<size_t>(n));
	for (int i = 0; i < n; ++i) {
		idx[static_cast<size_t>(i)] = i;
	}
	int guard = 0;
	while (idx.size() > 3 && guard < n * n) {
		++guard;
		const int m = static_cast<int>(idx.size());
		bool found = false;
		for (int i = 0; i < m; ++i) {
			const int ai = idx[static_cast<size_t>((i + m - 1) % m)];
			const int bi = idx[static_cast<size_t>(i)];
			const int ci = idx[static_cast<size_t>((i + 1) % m)];
			const Vec2 a = poly[static_cast<size_t>(ai)];
			const Vec2 b = poly[static_cast<size_t>(bi)];
			const Vec2 c = poly[static_cast<size_t>(ci)];
			if (cross_z(a, b, c) <= 0.0f) {
				continue; // reflex or colinear
			}
			bool any_inside = false;
			for (int k = 0; k < m; ++k) {
				if (k == (i + m - 1) % m || k == i || k == (i + 1) % m) continue;
				const Vec2 p = poly[static_cast<size_t>(idx[static_cast<size_t>(k)])];
				if (triangle_contains(a, b, c, p)) {
					any_inside = true;
					break;
				}
			}
			if (any_inside) {
				continue;
			}
			out_indices.push_back(ai);
			out_indices.push_back(bi);
			out_indices.push_back(ci);
			idx.erase(idx.begin() + i);
			found = true;
			break;
		}
		if (!found) {
			return false;
		}
	}
	if (idx.size() == 3) {
		out_indices.push_back(idx[0]);
		out_indices.push_back(idx[1]);
		out_indices.push_back(idx[2]);
	}
	return true;
}

namespace {

void emit_sector_floor_and_ceiling(std::vector<LitVertex>& out, const Sector& s)
{
	std::vector<int> tris;
	if (!triangulate_polygon(s.polygon, tris)) {
		return;
	}
	for (size_t i = 0; i + 2 < tris.size(); i += 3) {
		const Vec2& p0 = s.polygon[static_cast<size_t>(tris[i])];
		const Vec2& p1 = s.polygon[static_cast<size_t>(tris[i + 1])];
		const Vec2& p2 = s.polygon[static_cast<size_t>(tris[i + 2])];
		emit_floor_tri(out, p0, p1, p2, s.floor_y);
		emit_ceiling_tri(out, p0, p1, p2, s.ceiling_y);
	}
}

// ---------------- Wall prism (thick segments) ------------------------------------------------

Vec2 lerp_vec2(Vec2 a, Vec2 b, float t)
{
	return Vec2{a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t};
}

/// Emit a vertical quad from world point `a` to `b` spanning [y0, y1] with the given outward
/// normal. U coordinate runs from u0 at `a` to u1 at `b`; V follows world Y.
void emit_vertical_quad(
	std::vector<LitVertex>& out,
	Vec2 a, Vec2 b,
	float y0, float y1,
	float u0, float u1,
	float nx, float nz,
	uint32_t abgr)
{
	if (y1 <= y0) {
		return;
	}
	const float v0 = y0 * k_wall_uv_scale;
	const float v1 = y1 * k_wall_uv_scale;
	push_v(out, a.x, y0, a.z, nx, 0, nz, u0, v0, abgr);
	push_v(out, b.x, y0, b.z, nx, 0, nz, u1, v0, abgr);
	push_v(out, b.x, y1, b.z, nx, 0, nz, u1, v1, abgr);
	push_v(out, a.x, y0, a.z, nx, 0, nz, u0, v0, abgr);
	push_v(out, b.x, y1, b.z, nx, 0, nz, u1, v1, abgr);
	push_v(out, a.x, y1, a.z, nx, 0, nz, u0, v1, abgr);
}

/// Emit a horizontal quad (y fixed) covering the rectangle a→b→c→d with the given Y-normal
/// direction (+Y for floors, -Y for ceilings / door heads / wall tops viewed from below).
void emit_horizontal_quad(
	std::vector<LitVertex>& out,
	Vec2 a, Vec2 b, Vec2 c, Vec2 d,
	float y,
	float ny,
	uint32_t abgr)
{
	const auto v = [&](Vec2 p) {
		push_v(out, p.x, y, p.z, 0.0f, ny, 0.0f,
			p.x * k_floor_uv_per_world, p.z * k_floor_uv_per_world, abgr);
	};
	v(a); v(b); v(c);
	v(a); v(c); v(d);
}

/// Emit one vertical "face" of a thick wall — a quad along the segment `(a, b)` spanning
/// [y0, y1] — with the variant cutout applied (door opening, broken-wall trim, window slit).
/// `nx, nz` is the outward normal for this face.
void emit_face_with_variant(
	std::vector<LitVertex>& out,
	const Wall& w,
	Vec2 a, Vec2 b,
	float seg_len,
	float nx, float nz)
{
	const float u_total = seg_len * k_wall_uv_scale;

	switch (w.type) {
	case WallType::Normal:
		emit_vertical_quad(out, a, b, w.y0, w.y1, 0.0f, u_total, nx, nz, k_wall_abgr);
		break;
	case WallType::Door: {
		float door_w = w.door_width;
		if (door_w > seg_len) door_w = seg_len;
		float door_off = w.door_offset;
		if (door_off < 0.0f) door_off = 0.5f * (seg_len - door_w);
		if (door_off < 0.0f) door_off = 0.0f;
		if (door_off + door_w > seg_len) door_off = seg_len - door_w;
		const float t0 = door_off / seg_len;
		const float t1 = (door_off + door_w) / seg_len;
		const float door_top_y = w.y0 + w.door_height;
		const float top_y = door_top_y < w.y1 ? door_top_y : w.y1;

		const Vec2 p0 = lerp_vec2(a, b, t0);
		const Vec2 p1 = lerp_vec2(a, b, t1);
		if (t0 > 0.0f) {
			emit_vertical_quad(out, a, p0, w.y0, w.y1, 0.0f, t0 * u_total, nx, nz, k_wall_abgr);
		}
		if (t1 < 1.0f) {
			emit_vertical_quad(out, p1, b, w.y0, w.y1, t1 * u_total, u_total, nx, nz, k_wall_abgr);
		}
		if (w.y1 > top_y) {
			emit_vertical_quad(out, p0, p1, top_y, w.y1, t0 * u_total, t1 * u_total, nx, nz, k_wall_abgr);
		}
		break;
	}
	case WallType::Broken: {
		float top = w.y0 + k_broken_wall_height;
		if (top > w.y1) top = w.y1;
		emit_vertical_quad(out, a, b, w.y0, top, 0.0f, u_total, nx, nz, k_wall_abgr);
		break;
	}
	case WallType::Window: {
		const float sill = w.y0 + k_window_sill_h;
		const float wtop = w.y0 + k_window_top_h;
		const float s = sill < w.y1 ? sill : w.y1;
		const float t = wtop < w.y1 ? wtop : w.y1;
		if (s > w.y0) {
			emit_vertical_quad(out, a, b, w.y0, s, 0.0f, u_total, nx, nz, k_wall_abgr);
		}
		if (w.y1 > t) {
			emit_vertical_quad(out, a, b, t, w.y1, 0.0f, u_total, nx, nz, k_wall_abgr);
		}
		break;
	}
	}
}

/// Returns the top Y of the wall body — below any lintel/remnant limit. Controls how tall the
/// top cap / end caps of the prism are.
float wall_body_top_y(const Wall& w)
{
	if (w.type == WallType::Broken) {
		const float top = w.y0 + k_broken_wall_height;
		return top < w.y1 ? top : w.y1;
	}
	return w.y1;
}

void emit_wall(std::vector<LitVertex>& out, const Wall& w)
{
	const float dx = w.b.x - w.a.x;
	const float dz = w.b.z - w.a.z;
	const float len = std::sqrt(dx * dx + dz * dz);
	if (len <= 1e-6f || w.y1 <= w.y0) {
		return;
	}

	const float half_t = 0.5f * w.thickness;
	if (half_t <= 1e-4f) {
		// Zero thickness — emit a single quad (old behavior, double-sided via abs() shader).
		const float nx = -dz / len;
		const float nz = dx / len;
		emit_face_with_variant(out, w, w.a, w.b, len, nx, nz);
		return;
	}

	// Unit direction and outward perpendicular (left side has normal +perp, right side -perp).
	const float ux = dx / len;
	const float uz = dz / len;
	const float px = -uz;
	const float pz = ux;

	const Vec2 a_left{w.a.x + px * half_t, w.a.z + pz * half_t};
	const Vec2 b_left{w.b.x + px * half_t, w.b.z + pz * half_t};
	const Vec2 a_right{w.a.x - px * half_t, w.a.z - pz * half_t};
	const Vec2 b_right{w.b.x - px * half_t, w.b.z - pz * half_t};

	// Left and right side faces carry the variant's cutouts.
	emit_face_with_variant(out, w, a_left, b_left, len, px, pz);
	emit_face_with_variant(out, w, a_right, b_right, len, -px, -pz);

	// Top cap (at body_top_y) so you don't see into the wall from above.
	const float body_top = wall_body_top_y(w);
	if (body_top > w.y0) {
		emit_horizontal_quad(out, a_left, b_left, b_right, a_right, body_top, 1.0f, k_wall_abgr);
	}

	// End caps seal the segment endpoints. Small overlap with neighboring walls at corners is
	// acceptable for a 0.2m-ish thickness; tidy miters can come later from a junction graph.
	if (body_top > w.y0) {
		emit_vertical_quad(out, a_left, a_right, w.y0, body_top, 0.0f, w.thickness * k_wall_uv_scale,
			-ux, -uz, k_wall_abgr);
		emit_vertical_quad(out, b_right, b_left, w.y0, body_top, 0.0f, w.thickness * k_wall_uv_scale,
			ux, uz, k_wall_abgr);
	}

	// Door jambs — interior surfaces of the opening (left / right / head).
	if (w.type == WallType::Door) {
		float door_w = w.door_width;
		if (door_w > len) door_w = len;
		float door_off = w.door_offset;
		if (door_off < 0.0f) door_off = 0.5f * (len - door_w);
		if (door_off < 0.0f) door_off = 0.0f;
		if (door_off + door_w > len) door_off = len - door_w;
		const float t0 = door_off / len;
		const float t1 = (door_off + door_w) / len;
		const float door_top_y = w.y0 + w.door_height;
		const float head_y = door_top_y < w.y1 ? door_top_y : w.y1;

		const Vec2 p0_left = lerp_vec2(a_left, b_left, t0);
		const Vec2 p0_right = lerp_vec2(a_right, b_right, t0);
		const Vec2 p1_left = lerp_vec2(a_left, b_left, t1);
		const Vec2 p1_right = lerp_vec2(a_right, b_right, t1);

		if (head_y > w.y0) {
			// Near jamb (at t0). Outward normal faces into the opening (+ascent direction).
			emit_vertical_quad(out, p0_left, p0_right, w.y0, head_y,
				0.0f, w.thickness * k_wall_uv_scale, ux, uz, k_wall_abgr);
			// Far jamb (at t1). Outward normal faces into the opening (-ascent direction).
			emit_vertical_quad(out, p1_right, p1_left, w.y0, head_y,
				0.0f, w.thickness * k_wall_uv_scale, -ux, -uz, k_wall_abgr);
			// Door head — ceiling inside the opening, facing down.
			emit_horizontal_quad(out, p0_left, p1_left, p1_right, p0_right, head_y, -1.0f, k_wall_abgr);
		}
	}
}

// ---------------- Stair geometry ------------------------------------------------------------

void emit_stair_step_box(
	std::vector<LitVertex>& out,
	Vec2 a0, Vec2 b0, Vec2 a1, Vec2 b1,
	float y_bottom, float y_top,
	bool emit_back)
{
	// Corners (CCW top-down layout): a0 = near-left, b0 = near-right, b1 = far-right, a1 = far-left.
	// "Near" = previous step, "Far" = next step (ascending direction).
	// Emit: top (+Y), front (-ascent_dir side), left, right, and optionally back.
	const auto add = [&](float x, float y, float z, float nx, float ny, float nz, uint32_t abgr) {
		push_v(out, x, y, z, nx, ny, nz,
			x * k_floor_uv_per_world, z * k_floor_uv_per_world, abgr);
	};

	// Top surface at y_top.
	add(a0.x, y_top, a0.z, 0, 1, 0, k_stair_abgr);
	add(b1.x, y_top, b1.z, 0, 1, 0, k_stair_abgr);
	add(b0.x, y_top, b0.z, 0, 1, 0, k_stair_abgr);
	add(a0.x, y_top, a0.z, 0, 1, 0, k_stair_abgr);
	add(a1.x, y_top, a1.z, 0, 1, 0, k_stair_abgr);
	add(b1.x, y_top, b1.z, 0, 1, 0, k_stair_abgr);

	// Front riser at y_bottom..y_top along edge a0-b0. Normal = away from step top (toward -ascent).
	const float dx_f = b0.x - a0.x;
	const float dz_f = b0.z - a0.z;
	const float len_f = std::sqrt(dx_f * dx_f + dz_f * dz_f);
	// Direction from a0->a1 is ascent; perpendicular rotated is (-dz_asc, 0, dx_asc).
	const float ax = a1.x - a0.x;
	const float az = a1.z - a0.z;
	const float la = std::sqrt(ax * ax + az * az);
	const float ascent_x = la > 1e-6f ? ax / la : 0.0f;
	const float ascent_z = la > 1e-6f ? az / la : 0.0f;
	// Front face normal is -ascent direction.
	const float fn_x = -ascent_x;
	const float fn_z = -ascent_z;
	if (len_f > 1e-6f && y_top > y_bottom) {
		const uint32_t abgr = k_stair_abgr;
		add(a0.x, y_bottom, a0.z, fn_x, 0, fn_z, abgr);
		add(b0.x, y_bottom, b0.z, fn_x, 0, fn_z, abgr);
		add(b0.x, y_top, b0.z, fn_x, 0, fn_z, abgr);
		add(a0.x, y_bottom, a0.z, fn_x, 0, fn_z, abgr);
		add(b0.x, y_top, b0.z, fn_x, 0, fn_z, abgr);
		add(a0.x, y_top, a0.z, fn_x, 0, fn_z, abgr);
	}

	// Side faces (perpendicular sides from a0->a1 on the left, and b0->b1 on the right).
	// Side normals are perpendicular to ascent, pointing outward.
	// Left side normal = -perpendicular(ascent) = (-(-ascent_z), 0, -ascent_x) = (ascent_z, 0, -ascent_x)
	// Hmm, perpendicular(ascent) rotated 90deg CCW: (-ascent_z, 0, ascent_x)
	// Left side (from a0 side, not from b0 side) faces -perp = (ascent_z, 0, -ascent_x).
	{
		const float nx = ascent_z;
		const float nz = -ascent_x;
		const uint32_t abgr = k_stair_abgr;
		add(a0.x, y_bottom, a0.z, nx, 0, nz, abgr);
		add(a0.x, y_top, a0.z, nx, 0, nz, abgr);
		add(a1.x, y_top, a1.z, nx, 0, nz, abgr);
		add(a0.x, y_bottom, a0.z, nx, 0, nz, abgr);
		add(a1.x, y_top, a1.z, nx, 0, nz, abgr);
		add(a1.x, y_bottom, a1.z, nx, 0, nz, abgr);
	}
	{
		const float nx = -ascent_z;
		const float nz = ascent_x;
		const uint32_t abgr = k_stair_abgr;
		add(b0.x, y_bottom, b0.z, nx, 0, nz, abgr);
		add(b1.x, y_top, b1.z, nx, 0, nz, abgr);
		add(b0.x, y_top, b0.z, nx, 0, nz, abgr);
		add(b0.x, y_bottom, b0.z, nx, 0, nz, abgr);
		add(b1.x, y_bottom, b1.z, nx, 0, nz, abgr);
		add(b1.x, y_top, b1.z, nx, 0, nz, abgr);
	}

	if (emit_back) {
		// Back face (far side, ascent direction facing).
		const float nx = ascent_x;
		const float nz = ascent_z;
		const uint32_t abgr = k_stair_abgr;
		add(a1.x, y_bottom, a1.z, nx, 0, nz, abgr);
		add(a1.x, y_top, a1.z, nx, 0, nz, abgr);
		add(b1.x, y_top, b1.z, nx, 0, nz, abgr);
		add(a1.x, y_bottom, a1.z, nx, 0, nz, abgr);
		add(b1.x, y_top, b1.z, nx, 0, nz, abgr);
		add(b1.x, y_bottom, b1.z, nx, 0, nz, abgr);
	}
}

void emit_stair(std::vector<LitVertex>& out, const Stair& s)
{
	const float dx = s.center_b.x - s.center_a.x;
	const float dz = s.center_b.z - s.center_a.z;
	const float len = std::sqrt(dx * dx + dz * dz);
	if (len <= 1e-6f) {
		return;
	}
	const float ax = dx / len;
	const float az = dz / len;
	// Perpendicular unit vector (rotated 90 CCW, in XZ): (-az, 0, ax)
	const float px = -az;
	const float pz = ax;
	const float half_w = 0.5f * s.width;
	const int steps = s.steps < 1 ? 1 : s.steps;

	for (int i = 0; i < steps; ++i) {
		const float t0 = static_cast<float>(i) / static_cast<float>(steps);
		const float t1 = static_cast<float>(i + 1) / static_cast<float>(steps);
		const Vec2 mid0{s.center_a.x + dx * t0, s.center_a.z + dz * t0};
		const Vec2 mid1{s.center_a.x + dx * t1, s.center_a.z + dz * t1};
		const Vec2 a0{mid0.x - px * half_w, mid0.z - pz * half_w};
		const Vec2 b0{mid0.x + px * half_w, mid0.z + pz * half_w};
		const Vec2 a1{mid1.x - px * half_w, mid1.z - pz * half_w};
		const Vec2 b1{mid1.x + px * half_w, mid1.z + pz * half_w};
		const float y_bottom = s.from_y;
		const float y_top = s.from_y + (s.to_y - s.from_y) * t1;
		const bool is_last = (i == steps - 1);
		emit_stair_step_box(out, a0, b0, a1, b1, y_bottom, y_top, is_last);
	}
}

} // namespace

void build_level_meshes(const Level& level, LevelMeshOutput& out)
{
	out.floor_vertices.clear();
	out.wall_vertices.clear();
	out.stair_vertices.clear();

	for (const Sector& s : level.sectors) {
		emit_sector_floor_and_ceiling(out.floor_vertices, s);
	}
	for (const Wall& w : level.walls) {
		emit_wall(out.wall_vertices, w);
	}
	for (const Stair& s : level.stairs) {
		emit_stair(out.stair_vertices, s);
	}
}

} // namespace engine
