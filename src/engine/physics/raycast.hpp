#pragma once

#include "game/level/level_data.hpp"

#include <vector>

namespace engine {

// Slab-method ray vs axis-aligned box. Returns true and the entry distance
// (front-face hit) when the ray intersects in front of the origin. `t_hit` is
// only meaningful on success.
bool ray_aabb(float ox, float oy, float oz, float dx, float dy, float dz,
              float minx, float miny, float minz, float maxx, float maxy,
              float maxz, float &t_hit);

// Treats every wall as an opaque vertical quad on the XZ-plane segment a→b
// between y0..y1. Returns the smallest positive t in front of the ray. `t_hit`
// is only meaningful on success.
bool ray_walls_nearest(const std::vector<Wall> &walls, float ox, float oy, float oz,
                       float dx, float dy, float dz, float &t_hit);

// Ray vs capsule defined by segment A→B and radius r.
// Returns true and the entry distance t_hit if the ray hits in front of the origin.
bool ray_capsule(float ox, float oy, float oz,
                 float dx, float dy, float dz,
                 float ax, float ay, float az,
                 float bx, float by, float bz,
                 float r, float& t_hit);

} // namespace engine
