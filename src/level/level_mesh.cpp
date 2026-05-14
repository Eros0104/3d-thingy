#include "level/level_mesh.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace engine {

namespace {

constexpr float k_floor_uv_per_world = 1.0f / 6.0f;
constexpr float k_wall_uv_scale = 0.35f;
constexpr uint32_t k_floor_abgr = 0xffffffffu;
constexpr uint32_t k_wall_abgr = 0xffe8e4ddu;
constexpr uint32_t k_stair_abgr = 0xffd8d4ccu;
constexpr uint32_t k_frame_abgr = 0xff3c5a78u; // dark mahogany-ish wood trim
constexpr float k_frame_width = 0.12f;          // full strip width; half overlaps wall, half opening
constexpr float k_frame_push = 0.02f;           // offset along wall normal to avoid z-fighting

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

float cross_z(Vec2 a, Vec2 b, Vec2 c)
{
	return (b.x - a.x) * (c.z - b.z) - (b.z - a.z) * (c.x - b.x);
}

void emit_floor_tri(std::vector<LitVertex>& out, Vec2 a, Vec2 b, Vec2 c, float y)
{
	const auto add = [&](Vec2 p) {
		push_v(out, p.x, y, p.z, 0.0f, 1.0f, 0.0f,
			p.x * k_floor_uv_per_world, p.z * k_floor_uv_per_world, k_floor_abgr);
	};
	add(a); add(c); add(b);
}

void emit_ceiling_tri(std::vector<LitVertex>& out, Vec2 a, Vec2 b, Vec2 c, float y)
{
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
				continue;
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

// ---------------- Wall quad with CSG brush cutouts ------------------------------------------

/// Emit one vertical quad strip spanning [t0, t1] (parametric) horizontally and [y0, y1] vertically.
void emit_vertical_quad(
	std::vector<LitVertex>& out,
	Vec2 a, Vec2 b,
	float y0, float y1,
	float u0, float u1,
	float nx, float nz,
	uint32_t abgr)
{
	if (y1 <= y0 || u1 <= u0) {
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

/// A rectangular hole cut into a wall, in wall-local coordinates.
/// `frame` = true  → emit door/window trim strips around the opening.
/// `frame` = false → raw structural cut (ClippingCube), no trim.
struct CutRegion {
	float offset;   // world units along wall from a
	float width;    // horizontal extent
	float y_start;  // world Y of bottom edge
	float height;   // vertical extent
	bool  frame;
};

/// Emit a wall plane with zero or more rectangular cut-outs.
void emit_wall(std::vector<LitVertex>& out, const Wall& w, std::vector<CutRegion> cuts)
{
	const float dx = w.b.x - w.a.x;
	const float dz = w.b.z - w.a.z;
	const float horiz_len = std::sqrt(dx * dx + dz * dz);
	if (horiz_len <= 1e-6f || w.height <= 0.0f) {
		return;
	}

	// Outward normal perpendicular to the wall in the XZ plane.
	const float nx = -dz / horiz_len;
	const float nz = dx / horiz_len;

	const float base_y = w.a.y;
	const float top_y = base_y + w.height;
	const float total_u = horiz_len * k_wall_uv_scale;

	// Sort cuts left-to-right so we can walk the wall in one pass.
	std::sort(cuts.begin(), cuts.end(), [](const CutRegion& a, const CutRegion& b) {
		return a.offset < b.offset;
	});

	// Emit a quad strip at parametric positions [t0, t1] and world Y [y0, y1].
	const auto emit_strip = [&](float t0, float t1, float y0, float y1) {
		const Vec2 pa{w.a.x + dx * t0, w.a.z + dz * t0};
		const Vec2 pb{w.a.x + dx * t1, w.a.z + dz * t1};
		emit_vertical_quad(out, pa, pb, y0, y1,
			t0 * total_u, t1 * total_u, nx, nz, k_wall_abgr);
	};

	float cursor = 0.0f; // world-unit position along wall

	for (const CutRegion& cut : cuts) {
		const float c_off = std::max(0.0f, std::min(cut.offset,                horiz_len));
		const float c_end = std::max(c_off, std::min(cut.offset + cut.width,   horiz_len));
		const float c_y0  = std::max(cut.y_start,                base_y);
		const float c_y1  = std::min(cut.y_start + cut.height,  top_y);

		const float t_off = c_off / horiz_len;
		const float t_end = c_end / horiz_len;

		// Solid strip before this cut
		if (c_off > cursor + 1e-6f) {
			emit_strip(cursor / horiz_len, t_off, base_y, top_y);
		}
		// Solid above and below the opening
		if (c_y0 > base_y + 1e-6f) {
			emit_strip(t_off, t_end, base_y, c_y0);
		}
		if (c_y1 < top_y - 1e-6f) {
			emit_strip(t_off, t_end, c_y1, top_y);
		}

		cursor = c_end;
	}

	// Solid strip after the last cut
	if (cursor < horiz_len - 1e-6f) {
		emit_strip(cursor / horiz_len, 1.0f, base_y, top_y);
	}

	// Trim frames: only for brush cuts (doors/windows), not ClippingCube cuts.
	{
		const float hw_y = k_frame_width * 0.5f;
		const float ox = nx * k_frame_push;
		const float oz = nz * k_frame_push;

		const auto emit_frame_strip = [&](float t0, float t1, float y0, float y1) {
			t0 = std::max(0.0f, t0); t1 = std::min(1.0f, t1);
			y0 = std::max(base_y, y0); y1 = std::min(top_y, y1);
			if (t1 <= t0 + 1e-6f || y1 <= y0 + 1e-6f) return;
			const Vec2 pa{w.a.x + dx * t0 + ox, w.a.z + dz * t0 + oz};
			const Vec2 pb{w.a.x + dx * t1 + ox, w.a.z + dz * t1 + oz};
			emit_vertical_quad(out, pa, pb, y0, y1,
				t0 * total_u, t1 * total_u, nx, nz, k_frame_abgr);
		};

		for (const CutRegion& cut : cuts) {
			if (!cut.frame) continue;
			const float c_off = std::max(0.0f, std::min(cut.offset,             horiz_len));
			const float c_end = std::max(c_off, std::min(cut.offset + cut.width, horiz_len));
			const float c_y0  = std::max(cut.y_start,               base_y);
			const float c_y1  = std::min(cut.y_start + cut.height,  top_y);
			if (c_end - c_off < 1e-6f || c_y1 - c_y0 < 1e-6f) continue;

			const float t_off = c_off / horiz_len;
			const float t_end = c_end / horiz_len;
			const float hw_u  = hw_y / horiz_len;

			emit_frame_strip(t_off - hw_u, t_off + hw_u, c_y0 - hw_y, c_y1 + hw_y);
			emit_frame_strip(t_end - hw_u, t_end + hw_u, c_y0 - hw_y, c_y1 + hw_y);
			emit_frame_strip(t_off + hw_u, t_end - hw_u, c_y1 - hw_y, c_y1 + hw_y);
			if (c_y0 > base_y + 1e-6f) {
				emit_frame_strip(t_off + hw_u, t_end - hw_u, c_y0 - hw_y, c_y0 + hw_y);
			}
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

	// Front riser
	const float ax = a1.x - a0.x;
	const float az = a1.z - a0.z;
	const float la = std::sqrt(ax * ax + az * az);
	const float ascent_x = la > 1e-6f ? ax / la : 0.0f;
	const float ascent_z = la > 1e-6f ? az / la : 0.0f;
	const float fn_x = -ascent_x;
	const float fn_z = -ascent_z;
	const float len_f = std::sqrt((b0.x - a0.x) * (b0.x - a0.x) + (b0.z - a0.z) * (b0.z - a0.z));
	if (len_f > 1e-6f && y_top > y_bottom) {
		const uint32_t abgr = k_stair_abgr;
		add(a0.x, y_bottom, a0.z, fn_x, 0, fn_z, abgr);
		add(b0.x, y_bottom, b0.z, fn_x, 0, fn_z, abgr);
		add(b0.x, y_top, b0.z, fn_x, 0, fn_z, abgr);
		add(a0.x, y_bottom, a0.z, fn_x, 0, fn_z, abgr);
		add(b0.x, y_top, b0.z, fn_x, 0, fn_z, abgr);
		add(a0.x, y_top, a0.z, fn_x, 0, fn_z, abgr);
	}

	// Left side face
	{
		const float snx = ascent_z;
		const float snz = -ascent_x;
		const uint32_t abgr = k_stair_abgr;
		add(a0.x, y_bottom, a0.z, snx, 0, snz, abgr);
		add(a0.x, y_top, a0.z, snx, 0, snz, abgr);
		add(a1.x, y_top, a1.z, snx, 0, snz, abgr);
		add(a0.x, y_bottom, a0.z, snx, 0, snz, abgr);
		add(a1.x, y_top, a1.z, snx, 0, snz, abgr);
		add(a1.x, y_bottom, a1.z, snx, 0, snz, abgr);
	}
	// Right side face
	{
		const float snx = -ascent_z;
		const float snz = ascent_x;
		const uint32_t abgr = k_stair_abgr;
		add(b0.x, y_bottom, b0.z, snx, 0, snz, abgr);
		add(b1.x, y_top, b1.z, snx, 0, snz, abgr);
		add(b0.x, y_top, b0.z, snx, 0, snz, abgr);
		add(b0.x, y_bottom, b0.z, snx, 0, snz, abgr);
		add(b1.x, y_bottom, b1.z, snx, 0, snz, abgr);
		add(b1.x, y_top, b1.z, snx, 0, snz, abgr);
	}

	if (emit_back) {
		const float snx = ascent_x;
		const float snz = ascent_z;
		const uint32_t abgr = k_stair_abgr;
		add(a1.x, y_bottom, a1.z, snx, 0, snz, abgr);
		add(a1.x, y_top, a1.z, snx, 0, snz, abgr);
		add(b1.x, y_top, b1.z, snx, 0, snz, abgr);
		add(a1.x, y_bottom, a1.z, snx, 0, snz, abgr);
		add(b1.x, y_top, b1.z, snx, 0, snz, abgr);
		add(b1.x, y_bottom, b1.z, snx, 0, snz, abgr);
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
		const float y_bottom = s.from_y + (s.to_y - s.from_y) * t0;
		const float y_top = s.from_y + (s.to_y - s.from_y) * t1;
		emit_stair_step_box(out, a0, b0, a1, b1, y_bottom, y_top, true);
	}
}

/// Emit a horizontal quad (header or sill between paired walls) with a Y-axis normal.
void emit_horizontal_quad(
	std::vector<LitVertex>& out,
	Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3,
	float y, float ny, uint32_t abgr)
{
	push_v(out, p0.x, y, p0.z, 0.f, ny, 0.f, p0.x * k_floor_uv_per_world, p0.z * k_floor_uv_per_world, abgr);
	push_v(out, p1.x, y, p1.z, 0.f, ny, 0.f, p1.x * k_floor_uv_per_world, p1.z * k_floor_uv_per_world, abgr);
	push_v(out, p2.x, y, p2.z, 0.f, ny, 0.f, p2.x * k_floor_uv_per_world, p2.z * k_floor_uv_per_world, abgr);
	push_v(out, p0.x, y, p0.z, 0.f, ny, 0.f, p0.x * k_floor_uv_per_world, p0.z * k_floor_uv_per_world, abgr);
	push_v(out, p2.x, y, p2.z, 0.f, ny, 0.f, p2.x * k_floor_uv_per_world, p2.z * k_floor_uv_per_world, abgr);
	push_v(out, p3.x, y, p3.z, 0.f, ny, 0.f, p3.x * k_floor_uv_per_world, p3.z * k_floor_uv_per_world, abgr);
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

	// Group brushes by wall index for O(walls) dispatch.
	std::vector<std::vector<const Brush*>> wall_brushes(level.walls.size());
	for (const Brush& br : level.brushes) {
		if (br.wall >= 0 && br.wall < static_cast<int>(level.walls.size())) {
			wall_brushes[static_cast<size_t>(br.wall)].push_back(&br);
		}
	}

	for (size_t i = 0; i < level.walls.size(); ++i) {
		const Wall& w = level.walls[i];
		std::vector<CutRegion> cuts;

		// Brush cuts (door/window openings with trim frames).
		for (const Brush* br : wall_brushes[i]) {
			cuts.push_back({ br->offset, br->width, br->y_start, br->height, true });
		}

		// ClippingCube cuts: project each AABB onto the wall plane and compute overlap.
		const float ddx = w.b.x - w.a.x, ddz = w.b.z - w.a.z;
		const float wlen = std::sqrt(ddx * ddx + ddz * ddz);
		if (wlen > 1e-6f) {
			const float ux = ddx / wlen, uz = ddz / wlen;
			const float nx = -uz,         nz =  ux;
			const float base_y = w.a.y,   top_y = base_y + w.height;

			for (const ClippingCube& cc : level.clipping_cubes) {
				// Distance from cube center to the wall's infinite plane.
				const float d_perp = nx * (cc.pos.x - w.a.x) + nz * (cc.pos.z - w.a.z);
				// Maximum reach of the AABB perpendicular to the wall.
				const float r_perp = std::abs(nx) * cc.half_extents.x
				                   + std::abs(nz) * cc.half_extents.z;
				if (std::abs(d_perp) > r_perp) continue; // cube doesn't reach this wall

				// Along-wall projection of the cube.
				const float t_c = ux * (cc.pos.x - w.a.x) + uz * (cc.pos.z - w.a.z);
				const float r_t = std::abs(ux) * cc.half_extents.x
				                + std::abs(uz) * cc.half_extents.z;
				const float t0  = std::max(0.0f, t_c - r_t);
				const float t1  = std::min(wlen, t_c + r_t);
				if (t1 <= t0 + 1e-6f) continue;

				// Vertical overlap.
				const float y0 = std::max(base_y, cc.pos.y - cc.half_extents.y);
				const float y1 = std::min(top_y,  cc.pos.y + cc.half_extents.y);
				if (y1 <= y0 + 1e-6f) continue;

				cuts.push_back({ t0, t1 - t0, y0, y1 - y0, false });
			}
		}

		emit_wall(out.wall_vertices, w, std::move(cuts));
	}

	// Depth panels: close the gap between paired parallel-wall openings.
	// Two brushes are "paired" when their world-space opening edges are within
	// k_pair_tol of each other — produced by the editor carving both faces of a thick wall.
	{
		struct BrushEdge {
			Vec2 left, right;
			float y0, y1, base_y;
			float ux, uz;
			int wall_idx;
		};

		std::vector<BrushEdge> bedges;
		bedges.reserve(level.brushes.size());
		for (const Brush& br : level.brushes) {
			if (br.wall < 0 || br.wall >= static_cast<int>(level.walls.size())) continue;
			const Wall& w = level.walls[static_cast<size_t>(br.wall)];
			const float ddx = w.b.x - w.a.x, ddz = w.b.z - w.a.z;
			const float llen = std::sqrt(ddx * ddx + ddz * ddz);
			if (llen < 1e-6f) continue;
			const float uux = ddx / llen, uuz = ddz / llen;
			BrushEdge e;
			e.left  = {w.a.x + uux * br.offset,               w.a.z + uuz * br.offset};
			e.right = {w.a.x + uux * (br.offset + br.width),  w.a.z + uuz * (br.offset + br.width)};
			e.y0 = br.y_start;  e.y1 = br.y_start + br.height;
			e.base_y = w.a.y;
			e.ux = uux;  e.uz = uuz;
			e.wall_idx = br.wall;
			bedges.push_back(e);
		}

		constexpr float k_pair_tol_sq = 0.35f * 0.35f;
		const auto dsq = [](Vec2 p, Vec2 q) {
			return (p.x - q.x) * (p.x - q.x) + (p.z - q.z) * (p.z - q.z);
		};

		std::vector<bool> used(bedges.size(), false);
		for (size_t i = 0; i < bedges.size(); ++i) {
			if (used[i]) continue;
			for (size_t j = i + 1; j < bedges.size(); ++j) {
				if (used[j]) continue;
				const BrushEdge& e1 = bedges[i];
				const BrushEdge& e2 = bedges[j];
				if (e1.wall_idx == e2.wall_idx) continue;
				const bool fwd = dsq(e1.left,  e2.left)  <= k_pair_tol_sq &&
				                 dsq(e1.right, e2.right) <= k_pair_tol_sq;
				const bool rev = dsq(e1.left,  e2.right) <= k_pair_tol_sq &&
				                 dsq(e1.right, e2.left)  <= k_pair_tol_sq;
				if (!fwd && !rev) continue;
				const float y0 = std::max(e1.y0, e2.y0);
				const float y1 = std::min(e1.y1, e2.y1);
				if (y1 <= y0 + 1e-6f) continue;

				// For fwd pairs both lefts align; for rev pairs e1.left aligns with e2.right.
				const Vec2& near_left  = fwd ? e2.left  : e2.right;
				const Vec2& near_right = fwd ? e2.right : e2.left;
				const float depth   = std::sqrt(dsq(e1.left, near_left));
				const float u_depth = depth * k_wall_uv_scale;

				// Left jamb: vertical fill at the opening's left edge, facing into the passage
				emit_vertical_quad(out.wall_vertices, e1.left,  near_left,  y0, y1, 0.f, u_depth,  e1.ux,  e1.uz, k_frame_abgr);
				// Right jamb: vertical fill at the opening's right edge
				emit_vertical_quad(out.wall_vertices, near_right, e1.right, y0, y1, 0.f, u_depth, -e1.ux, -e1.uz, k_frame_abgr);
				// Header: horizontal cap closing the top of the opening gap
				emit_horizontal_quad(out.wall_vertices, e1.left, e1.right, near_right, near_left, y1, -1.f, k_frame_abgr);

				// Sill: only for windows (opening not flush with wall base)
				const bool is_window = e1.y0 > e1.base_y + 1e-6f || e2.y0 > e2.base_y + 1e-6f;
				if (is_window) {
					emit_horizontal_quad(out.wall_vertices, near_left, near_right, e1.right, e1.left, y0, 1.f, k_frame_abgr);
				}

				used[i] = used[j] = true;
				break;
			}
		}
	}

	for (const Stair& s : level.stairs) {
		emit_stair(out.stair_vertices, s);
	}
}

} // namespace engine
