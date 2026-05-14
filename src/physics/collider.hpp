#pragma once

namespace engine {

struct Collider {
    float radius  = 0.28f;
    float height  = 1.6f;
    float step_up = 1.0f;

    constexpr void capsule_endpoints(float cx, float feet_y, float cz,
                                     float out_a[3], float out_b[3]) const noexcept {
        out_a[0] = cx; out_a[1] = feet_y + radius;          out_a[2] = cz;
        out_b[0] = cx; out_b[1] = feet_y + height - radius; out_b[2] = cz;
    }
};

} // namespace engine
