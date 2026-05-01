#include "engine/physics/raycast.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine {

bool ray_aabb(float ox, float oy, float oz, float dx, float dy, float dz,
              float minx, float miny, float minz, float maxx, float maxy,
              float maxz, float &t_hit) {
  const float inv_dx = (std::fabs(dx) > 1e-8f)
                           ? 1.0f / dx
                           : std::numeric_limits<float>::infinity();
  const float inv_dy = (std::fabs(dy) > 1e-8f)
                           ? 1.0f / dy
                           : std::numeric_limits<float>::infinity();
  const float inv_dz = (std::fabs(dz) > 1e-8f)
                           ? 1.0f / dz
                           : std::numeric_limits<float>::infinity();

  const float t1 = (minx - ox) * inv_dx;
  const float t2 = (maxx - ox) * inv_dx;
  const float t3 = (miny - oy) * inv_dy;
  const float t4 = (maxy - oy) * inv_dy;
  const float t5 = (minz - oz) * inv_dz;
  const float t6 = (maxz - oz) * inv_dz;

  const float t_near =
      std::max({std::min(t1, t2), std::min(t3, t4), std::min(t5, t6)});
  const float t_far =
      std::min({std::max(t1, t2), std::max(t3, t4), std::max(t5, t6)});

  if (t_far < 0.0f || t_near > t_far) {
    return false;
  }
  t_hit = t_near >= 0.0f ? t_near : t_far;
  return t_hit >= 0.0f;
}

bool ray_walls_nearest(const std::vector<Wall> &walls, float ox, float oy, float oz,
                       float dx, float dy, float dz, float &t_hit) {
  bool any = false;
  float best_t = std::numeric_limits<float>::infinity();
  for (const Wall &w : walls) {
    const float bxx = w.b.x - w.a.x;
    const float bzz = w.b.z - w.a.z;
    const float det = dx * bzz - dz * bxx;
    if (std::fabs(det) < 1e-7f) {
      continue; // ray parallel to wall
    }
    const float ax_ox = w.a.x - ox;
    const float az_oz = w.a.z - oz;
    // Parameter s along wall (0..1), parameter t along ray (>= 0).
    const float s = (-dz * (-ax_ox) + dx * (-az_oz)) / det;
    const float t = (bxx * (-az_oz) - bzz * (-ax_ox)) / det;
    if (t <= 0.0f || s < 0.0f || s > 1.0f) {
      continue;
    }
    const float y_at = oy + dy * t;
    if (y_at < w.y0 || y_at > w.y1) {
      continue;
    }
    if (t < best_t) {
      best_t = t;
      any = true;
    }
  }
  t_hit = best_t;
  return any;
}

} // namespace engine
