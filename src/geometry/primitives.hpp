#pragma once

#include "lit_vertex.hpp"

#include <cstdint>
#include <vector>

namespace engine {

// Builds an axis-aligned unit cube of side 2*half_extent, centered at the
// origin. Output is six quads (two triangles each) with per-face normals,
// 0..1 UVs, and a uniform vertex color packed as ABGR.
void build_unit_cube(std::vector<LitVertex> &vertices,
                     std::vector<uint16_t> &indices, float half_extent,
                     uint32_t abgr);

// Builds a UV sphere of the given radius centered at the origin.
// `stacks` controls latitude bands, `slices` controls longitude bands. The
// vertex color is packed as ABGR and applied uniformly to every vertex.
void build_uv_sphere(std::vector<LitVertex> &vertices,
                     std::vector<uint16_t> &indices, float radius, int stacks,
                     int slices, uint32_t abgr);

} // namespace engine
