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

// ---------------- Wall quads (double-sided) -------------------------------------------------

/// Emit a single vertical quad along the line from point a to point b, at heights [y0, y1].
/// Zero-thickness: rendered with no face culling and two-sided lighting (fragment shader uses
/// |dot(N, L)|), so a single quad is visible and lit from either side without z-fighting.
void emit_wall_quad(
	std::vector<LitVertex>& out,
	Vec2 a, Vec2 b,
	float y0, float y1,
	float u0, float u1,
	uint32_t abgr)
{
	const float dx = b.x - a.x;
	const float dz = b.z - a.z;
	const float len = std::sqrt(dx * dx + dz * dz);
	if (len <= 1e-6f || y1 <= y0) {
		return;
	}
	const float nx = -dz / len;
	const float nz = dx / len;
	const float v0 = y0 * k_wall_uv_scale;
	const float v1 = y1 * k_wall_uv_scale;

	push_v(out, a.x, y0, a.z, nx, 0, nz, u0, v0, abgr);
	push_v(out, b.x, y0, b.z, nx, 0, nz, u1, v0, abgr);
	push_v(out, b.x, y1, b.z, nx, 0, nz, u1, v1, abgr);
	push_v(out, a.x, y0, a.z, nx, 0, nz, u0, v0, abgr);
	push_v(out, b.x, y1, b.z, nx, 0, nz, u1, v1, abgr);
	push_v(out, a.x, y1, a.z, nx, 0, nz, u0, v1, abgr);
}

Vec2 lerp_vec2(Vec2 a, Vec2 b, float t)
{
	return Vec2{a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t};
}

void emit_wall(std::vector<LitVertex>& out, const Wall& w)
{
	const float dx = w.b.x - w.a.x;
	const float dz = w.b.z - w.a.z;
	const float len = std::sqrt(dx * dx + dz * dz);
	if (len <= 1e-6f || w.y1 <= w.y0) {
		return;
	}
	const float u_total = len * k_wall_uv_scale;

	switch (w.type) {
	case WallType::Normal:
		emit_wall_quad(out, w.a, w.b, w.y0, w.y1, 0.0f, u_total, k_wall_abgr);
		break;
	case WallType::Door: {
		float door_w = w.door_width;
		if (door_w > len) door_w = len;
		float door_off = w.door_offset;
		if (door_off < 0.0f) {
			door_off = 0.5f * (len - door_w);
		}
		if (door_off < 0.0f) door_off = 0.0f;
		if (door_off + door_w > len) door_off = len - door_w;
		const float t0 = door_off / len;
		const float t1 = (door_off + door_w) / len;
		const float door_top_y = w.y0 + w.door_height;
		const float top_y = door_top_y < w.y1 ? door_top_y : w.y1;

		const Vec2 l = w.a;
		const Vec2 p0 = lerp_vec2(w.a, w.b, t0);
		const Vec2 p1 = lerp_vec2(w.a, w.b, t1);
		const Vec2 r = w.b;

		// Left strip
		if (t0 > 0.0f) {
			emit_wall_quad(out, l, p0, w.y0, w.y1, 0.0f, t0 * u_total, k_wall_abgr);
		}
		// Right strip
		if (t1 < 1.0f) {
			emit_wall_quad(out, p1, r, w.y0, w.y1, t1 * u_total, u_total, k_wall_abgr);
		}
		// Lintel above door
		if (w.y1 > top_y) {
			emit_wall_quad(out, p0, p1, top_y, w.y1, t0 * u_total, t1 * u_total, k_wall_abgr);
		}
		break;
	}
	case WallType::Broken: {
		float top = w.y0 + k_broken_wall_height;
		if (top > w.y1) top = w.y1;
		emit_wall_quad(out, w.a, w.b, w.y0, top, 0.0f, u_total, k_wall_abgr);
		break;
	}
	case WallType::Window: {
		const float sill = w.y0 + k_window_sill_h;
		const float wtop = w.y0 + k_window_top_h;
		const float s = sill < w.y1 ? sill : w.y1;
		const float t = wtop < w.y1 ? wtop : w.y1;
		if (s > w.y0) {
			emit_wall_quad(out, w.a, w.b, w.y0, s, 0.0f, u_total, k_wall_abgr);
		}
		if (w.y1 > t) {
			emit_wall_quad(out, w.a, w.b, t, w.y1, 0.0f, u_total, k_wall_abgr);
		}
		break;
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
