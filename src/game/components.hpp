#pragma once

namespace game {

struct TransformC {
    float x = 0.f, y = 0.f, z = 0.f;
    float yaw = 0.f;
};

struct HealthC {
    int hp = 100;
    bool alive() const { return hp > 0; }
};

// Per-NPC walk/attack state for the zombie AI system.
struct ZombieAIC {
    float damage_timer    = 0.f;
    float hit_stun_timer  = 0.f;
    bool  dying           = false;
};

// Destructible target cube.
struct TargetC {
    float half_extent    = 0.4f;
    int   hits_remaining = 3;
    bool  alive          = true;
};

// Index into GameState::models_ (pool of Viewmodel instances).
struct SkinnedModelC {
    int model_idx = -1;
};

// Filter tags — presence is the only information they carry.
struct ZombieTag {};
struct TargetTag {};

} // namespace game
