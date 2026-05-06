#pragma once

namespace game {

struct TransformC {
    float x = 0.f, y = 0.f, z = 0.f;
    float yaw = 0.f;
};

struct TargetC {
    float half_extent    = 0.4f;
    int   hits_remaining = 3;
    bool  alive          = true;
};

struct TargetTag {};

} // namespace game
