#include "primitives.hpp"

#include <bx/math.h>

#include <cmath>
#include <cstddef>

namespace engine {

void build_unit_cube(std::vector<LitVertex> &vertices,
                     std::vector<uint16_t> &indices, float half_extent,
                     uint32_t abgr) {
  vertices.clear();
  indices.clear();

  const float h = half_extent;
  struct Face {
    float nx, ny, nz;
    float cx, cy, cz; // center offset along normal
    float ux, uy, uz; // tangent (full extent direction 1)
    float vx, vy, vz; // tangent (full extent direction 2)
  };
  const Face faces[6] = {
      {1, 0, 0, h, 0, 0, 0, 0, -1, 0, 1, 0},   // +X (right)
      {-1, 0, 0, -h, 0, 0, 0, 0, 1, 0, 1, 0},  // -X (left)
      {0, 1, 0, 0, h, 0, 1, 0, 0, 0, 0, 1},    // +Y (top)
      {0, -1, 0, 0, -h, 0, 1, 0, 0, 0, 0, -1}, // -Y (bottom)
      {0, 0, 1, 0, 0, h, 1, 0, 0, 0, 1, 0},    // +Z (front)
      {0, 0, -1, 0, 0, -h, -1, 0, 0, 0, 1, 0}, // -Z (back)
  };
  for (int f = 0; f < 6; ++f) {
    const Face &fc = faces[f];
    const uint16_t base = static_cast<uint16_t>(vertices.size());
    for (int j = 0; j < 4; ++j) {
      const float su = (j == 1 || j == 2) ? h : -h;
      const float sv = (j >= 2) ? h : -h;
      LitVertex v;
      v.x = fc.cx + fc.ux * su + fc.vx * sv;
      v.y = fc.cy + fc.uy * su + fc.vy * sv;
      v.z = fc.cz + fc.uz * su + fc.vz * sv;
      v.nx = fc.nx;
      v.ny = fc.ny;
      v.nz = fc.nz;
      v.u = (j == 1 || j == 2) ? 1.0f : 0.0f;
      v.v = (j >= 2) ? 1.0f : 0.0f;
      v.abgr = abgr;
      vertices.push_back(v);
    }
    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 0);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
  }
}

void build_uv_sphere(std::vector<LitVertex> &vertices,
                     std::vector<uint16_t> &indices, float radius, int stacks,
                     int slices, uint32_t abgr) {
  vertices.clear();
  indices.clear();
  vertices.reserve(static_cast<size_t>(stacks + 1) *
                   static_cast<size_t>(slices));
  for (int i = 0; i <= stacks; ++i) {
    const float phi = -bx::kPiHalf + (bx::kPi * static_cast<float>(i) /
                                      static_cast<float>(stacks));
    const float cos_phi = std::cos(phi);
    const float sin_phi = std::sin(phi);
    for (int j = 0; j < slices; ++j) {
      const float theta =
          (bx::kPi * 2.0f) * static_cast<float>(j) / static_cast<float>(slices);
      const float cos_theta = std::cos(theta);
      const float sin_theta = std::sin(theta);
      LitVertex v;
      v.x = radius * cos_phi * cos_theta;
      v.y = radius * sin_phi;
      v.z = radius * cos_phi * sin_theta;
      v.nx = cos_phi * cos_theta;
      v.ny = sin_phi;
      v.nz = cos_phi * sin_theta;
      v.u = static_cast<float>(j) / static_cast<float>(slices);
      v.v = static_cast<float>(i) / static_cast<float>(stacks);
      v.abgr = abgr;
      vertices.push_back(v);
    }
  }
  for (int i = 0; i < stacks; ++i) {
    for (int j = 0; j < slices; ++j) {
      const int jn = (j + 1) % slices;
      const uint16_t a = static_cast<uint16_t>(i * slices + j);
      const uint16_t b = static_cast<uint16_t>((i + 1) * slices + j);
      const uint16_t c = static_cast<uint16_t>((i + 1) * slices + jn);
      const uint16_t d = static_cast<uint16_t>(i * slices + jn);
      indices.push_back(a);
      indices.push_back(b);
      indices.push_back(c);
      indices.push_back(a);
      indices.push_back(c);
      indices.push_back(d);
    }
  }
}

} // namespace engine
