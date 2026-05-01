#pragma once

#include "game/level/level_data.hpp"
#include "engine/lit_vertex.hpp"

#include <vector>

namespace engine {

struct LevelMeshOutput {
	std::vector<LitVertex> floor_vertices;
	std::vector<LitVertex> wall_vertices;
	std::vector<LitVertex> stair_vertices;
};

/// Build CPU-side meshes for a `Level`. Sector polygons are triangulated (ear clipping) into
/// floor quads at `sector.floor_y`. Walls become vertical quads (both sides rendered) along
/// their segment, with variant-specific cutouts for doors/windows/broken walls. Stairs emit a
/// stack of step boxes between `from_y` and `to_y`.
void build_level_meshes(const Level& level, LevelMeshOutput& out);

/// Triangulate a simple (non-self-intersecting) counter-clockwise polygon by ear clipping.
/// Emits index triples into `out_indices` referring to `poly` positions. Returns false if
/// the polygon is degenerate. Exposed for editor/tool use.
bool triangulate_polygon(const std::vector<Vec2>& poly, std::vector<int>& out_indices);

} // namespace engine
