#pragma once

#include "game/animator.hpp"

namespace engine { class RiggedModel; }

namespace game {

struct TransformC {
    float x = 0.f, y = 0.f, z = 0.f;
    float yaw = 0.f;
};

struct HealthC {
    int hp = 100;
    bool alive() const { return hp > 0; }
};

struct ZombieAIC {
    static constexpr float k_radius = 0.35f;
    static constexpr float k_height = 1.8f;

    float damage_timer   = 0.f;
    float hit_stun_timer = 0.f;
    bool  dying          = false;
};

struct TargetC {
    float half_extent    = 0.4f;
    int   hits_remaining = 3;
    bool  alive          = true;
};

struct SkinnedModelC {
    engine::RiggedModel* model = nullptr;
    Animator             anim;
};

struct ZombieTag {};
struct TargetTag {};

} // namespace game
