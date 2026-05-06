#pragma once

namespace game {

struct Target {
    static constexpr float k_half_extent = 0.4f;
    static constexpr int   k_max_hits    = 3;

    float x             = 0.f;
    float y             = 0.f;
    float z             = 0.f;
    float half_extent   = k_half_extent;
    int   hits_remaining = k_max_hits;
    bool  alive          = true;

    void take_hit() {
        if (!alive) return;
        if (--hits_remaining <= 0)
            alive = false;
    }
};

} // namespace game
