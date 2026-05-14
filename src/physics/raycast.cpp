#include "physics/raycast.hpp"

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

bool ray_capsule(float ox, float oy, float oz,
                 float dx, float dy, float dz,
                 float ax, float ay, float az,
                 float bx, float by, float bz,
                 float r, float& t_hit) {
  const float nx = bx - ax, ny = by - ay, nz = bz - az;
  const float n_sq = nx*nx + ny*ny + nz*nz;

  const float mx = ox - ax, my = oy - ay, mz = oz - az;

  const float n_dot_d = nx*dx + ny*dy + nz*dz;
  const float n_dot_m = nx*mx + ny*my + nz*mz;

  const float a = n_sq - n_dot_d * n_dot_d;
  const float k = mx*mx + my*my + mz*mz - r*r;
  const float c = n_sq * k - n_dot_m * n_dot_m;

  float best = std::numeric_limits<float>::infinity();

  if (std::fabs(a) > 1e-8f) {
    const float b = n_sq * (dx*mx + dy*my + dz*mz) - n_dot_d * n_dot_m;
    const float disc = b*b - a*c;
    if (disc >= 0.0f) {
      const float sq = std::sqrt(disc);
      for (float sign : {-1.0f, 1.0f}) {
        const float t = (-b + sign * sq) / a;
        if (t < 0.0f) continue;
        const float proj = n_dot_m + t * n_dot_d;
        if (proj >= 0.0f && proj <= n_sq) {
          best = std::min(best, t);
          break;
        }
      }
    }
  }

  auto hit_sphere = [&](float cx, float cy, float cz) {
    const float smx = ox - cx, smy = oy - cy, smz = oz - cz;
    const float sb = dx*smx + dy*smy + dz*smz;
    const float sc = smx*smx + smy*smy + smz*smz - r*r;
    const float disc = sb*sb - sc;
    if (disc < 0.0f) return;
    const float sq = std::sqrt(disc);
    float t = -sb - sq;
    if (t < 0.0f) t = -sb + sq;
    if (t >= 0.0f) best = std::min(best, t);
  };
  hit_sphere(ax, ay, az);
  hit_sphere(bx, by, bz);

  if (best < std::numeric_limits<float>::infinity()) {
    t_hit = best;
    return true;
  }
  return false;
}

} // namespace engine
