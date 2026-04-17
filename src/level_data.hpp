#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace engine {

/// Canonical in-memory level representation (v2). Doom/Quake-style: 2D sector polygons on the
/// XZ plane define walkable areas, zero-thickness wall segments define visual + collision
/// boundaries, and explicit stair ramps connect sectors at different Y. Coordinates are
/// world-space meters (no cell/grid units). Axes: +X east, +Y up, +Z south.
///
/// JSON schema (see src/json_level.cpp) and .evil binary format (see src/level_binary.cpp)
/// both parse into this structure.

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
/// `polygon` winding is expected counter-clockwise when viewed from above (+Y looking down);
/// the loader reverses inward-wound polygons automatically.
struct Sector {
	std::string id;               ///< Optional name (e.g. "main_hall"); may be empty.
	std::vector<Vec2> polygon;    ///< >= 3 points; implicit close from last → first.
	float floor_y = 0.0f;
	float ceiling_y = 3.2f;
};

enum class WallType : uint8_t {
	Normal = 0,   ///< Full-height solid wall segment.
	Door = 1,     ///< Wall with a door-shaped cutout (a rectangular opening).
	Broken = 2,   ///< Half-height remnant (visual cover, not walkable).
	Window = 3,   ///< Wall with a mid-height slit opening.
};

bool wall_type_from_string(const std::string& s, WallType& out);
const char* wall_type_to_string(WallType t);

/// A wall is a zero-thickness line segment in the XZ plane. Visual + collision.
/// For `Door` variants: `door_width` is the opening width along the segment; `door_offset`
/// is the distance from endpoint `a` to the near edge of the opening. `door_height` is the
/// opening's height above `y0`.
struct Wall {
	WallType type = WallType::Normal;
	Vec2 a;
	Vec2 b;
	float y0 = 0.0f;              ///< Bottom of wall in world Y.
	float y1 = 3.2f;              ///< Top of wall in world Y.
	float thickness = 0.2f;       ///< Visual thickness perpendicular to the segment (meters).
	float door_width = 1.2f;      ///< Only meaningful when type == Door.
	float door_offset = -1.0f;    ///< < 0 means "center the opening along the segment".
	float door_height = 2.2f;     ///< Height of the opening above y0.
};

/// A stair is a rectangular ramp whose top surface rises linearly from `from_y` at endpoint
/// `center_a` to `to_y` at endpoint `center_b`. `width` is perpendicular full width (total,
/// not half). Collision/Y-interpolation treats the stair as a planar ramp; rendering optionally
/// emits discrete step boxes for the classic RE/Doom look (see `steps`).
struct Stair {
	Vec2 center_a;
	Vec2 center_b;
	float width = 2.0f;
	float from_y = 0.0f;
	float to_y = 3.2f;
	/// Number of discrete steps to emit visually. <= 1 means a smooth ramp.
	int steps = 8;
};

struct Light {
	Vec3 pos;
	std::array<float, 3> color = {2.4f, 2.1f, 1.7f};
	float intensity = 1.0f;
};

struct Spawn {
	Vec3 pos;
	float yaw_deg = 0.0f;
};

struct Level {
	int version = 2;
	std::string name;
	float wall_height = 3.2f;     ///< Default for walls that don't set y0/y1 explicitly.
	std::array<float, 3> ambient = {0.07f, 0.08f, 0.11f};
	Spawn spawn;
	std::vector<Sector> sectors;
	std::vector<Wall> walls;
	std::vector<Stair> stairs;
	std::vector<Light> lights;
};

/// Signed area of a 2D polygon (shoelace). Positive → counter-clockwise winding.
float polygon_signed_area(const std::vector<Vec2>& poly);

/// True if `p` lies inside `poly` (ray casting; boundary undefined).
bool point_in_polygon(const std::vector<Vec2>& poly, Vec2 p);

} // namespace engine
