#pragma once

#include <array>
#include <string>
#include <vector>

namespace engine {

/// Canonical in-memory level representation (v3). Sector polygons on the XZ plane define
/// walkable areas. Walls are simple planar quads defined by two 3-D base points and a height;
/// `Brush` entries carve rectangular openings (CSG-style). Stairs connect sectors at different Y.
/// Axes: +X east, +Y up, +Z south.

struct Vec2 {
	float x = 0.0f;
	float z = 0.0f;
};

struct Vec3 {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

/// A sector is a polygonal region in the XZ plane with its own floor/ceiling elevation.
/// Polygon winding is counter-clockwise viewed from above (+Y); the loader auto-fixes CW input.
struct Sector {
	std::string id;
	std::vector<Vec2> polygon;
	float floor_y = 0.0f;
	float ceiling_y = 3.2f;
};

/// A wall is a planar quad. `a` and `b` are the 3-D base points (bottom edge); the quad rises
/// `height` metres straight up. Parallel walls give visual thickness. Use `Level::brushes` to
/// carve door/window openings.
struct Wall {
	Vec3 a;
	Vec3 b;
	float height = 3.2f;
};

/// A rectangular opening carved into a wall. `wall` indexes into `Level::walls`.
/// `offset` is the distance along the wall from `a` to the near edge of the opening.
/// `y_start` is the world-Y of the opening bottom; `height` is the vertical extent.
struct Brush {
	int wall = 0;
	float offset = 0.0f;
	float width = 1.2f;
	float y_start = 0.0f;
	float height = 2.2f;
};

/// A stair ramp whose top surface rises from `from_y` at `center_a` to `to_y` at `center_b`.
/// `width` is the full perpendicular width. `steps` controls how many discrete step boxes render.
struct Stair {
	Vec2 center_a;
	Vec2 center_b;
	float width = 2.0f;
	float from_y = 0.0f;
	float to_y = 3.2f;
	int steps = 8;
};

struct Light {
	Vec3 pos;
	std::array<float, 3> color = {2.4f, 2.1f, 1.7f};
	float intensity = 1.0f;
	float radius = 10.0f; // world-units; light is zero beyond this distance
};

struct Spawn {
	Vec3 pos;
	float yaw_deg = 0.0f;
};

/// An axis-aligned box that carves a hole through any overlapping wall geometry.
/// Position is the world-space center; half_extents are the positive half-sizes on each axis.
struct ClippingCube {
	Vec3 pos;
	Vec3 half_extents{ 0.6f, 1.1f, 0.6f };
};

struct Level {
	int version = 3;
	std::string name;
	float wall_height = 3.2f;
	std::array<float, 3> ambient = {0.07f, 0.08f, 0.11f};
	Spawn spawn;
	std::vector<Sector> sectors;
	std::vector<Wall> walls;
	std::vector<Brush> brushes;
	std::vector<Stair> stairs;
	std::vector<Light> lights;
	std::vector<ClippingCube> clipping_cubes;
};

/// Signed area of a 2-D polygon (shoelace). Positive → counter-clockwise winding.
float polygon_signed_area(const std::vector<Vec2>& poly);

/// True if `p` lies inside `poly` (ray casting; boundary undefined).
bool point_in_polygon(const std::vector<Vec2>& poly, Vec2 p);

} // namespace engine
