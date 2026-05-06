#pragma once

#include "engine/rigged_model.hpp"
#include "game/animator.hpp"

#include <bgfx/bgfx.h>

namespace engine { struct Level; }

namespace game {

class Player;

class Zombie {
public:
    static constexpr float k_radius = 0.35f;
    static constexpr float k_height = 1.8f;

    Zombie(float x, float y, float z, engine::RiggedModel* model);

    void update(float dt, Player& player, const engine::Level& level);

    void render(
        bgfx::ViewId        view_id,
        bgfx::ProgramHandle program,
        bgfx::UniformHandle u_bones,
        bgfx::UniformHandle s_albedo,
        bgfx::UniformHandle u_baseColor,
        bgfx::TextureHandle fallback_white,
        uint64_t            state
    ) const;

    void take_damage(int amount);

    bool alive() const { return hp_ > 0; }

    // Capsule segment endpoints for raycasting.
    void capsule(float out_a[3], float out_b[3]) const;

    float x   = 0.f;
    float y   = 0.f;  // feet (floor level)
    float z   = 0.f;
    float yaw = 0.f;

private:
    int   hp_             = 100;
    float damage_timer_   = 0.f;
    float hit_stun_timer_ = 0.f;
    bool  dying_          = false;

    engine::RiggedModel* model_;
    Animator             anim_;
};

} // namespace game
