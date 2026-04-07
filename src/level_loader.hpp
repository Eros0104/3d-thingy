#pragma once

#include "lit_vertex.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

/// Row-major `tiles[r * width + c]`: `'#'` floor, `'x'` wall.
struct LoadedLevel {
	int width = 0;
	int height = 0;
	float cell_size = 2.0f;
	std::vector<char> tiles;
	/// World-space point light positions (from `*` on floor in lights file).
	std::vector<float> light_positions;

	bool in_bounds(int c, int r) const
	{
		return c >= 0 && c < width && r >= 0 && r < height;
	}

	char tile(int c, int r) const { return tiles[static_cast<size_t>(r) * static_cast<size_t>(width) + static_cast<size_t>(c)]; }

	bool is_floor(int c, int r) const { return in_bounds(c, r) && tile(c, r) == '#'; }
	bool is_wall(int c, int r) const { return in_bounds(c, r) && tile(c, r) == 'x'; }

	void world_to_cell(float wx, float wz, int& c, int& r) const;
	void cell_center_world(int col, int row, float& out_x, float& out_z) const;

	/// True if the cell under (wx, wz) is walkable floor.
	bool walkable_at_world(float wx, float wz) const;
};

/// Reads terrain (`x` / `#`) and optional lights (`.` / `*`, same dimensions as map).
bool load_evil_level(const char* map_path, const char* lights_path, LoadedLevel& out, std::string& err);

/// Builds lit meshes in world space (y = 0 floor). Walls are full-cell boxes from y = 0 .. wall_height.
void build_level_meshes(
	const LoadedLevel& level,
	float wall_height,
	std::vector<LitVertex>& out_floor_vertices,
	std::vector<LitVertex>& out_wall_vertices
);

} // namespace engine
