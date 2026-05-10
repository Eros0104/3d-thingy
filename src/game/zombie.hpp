#pragma once

#include "engine/collider.hpp"
#include "engine/physics/jolt_physics.hpp"
#include "engine/rigged_model.hpp"
#include "game/animator.hpp"

#include <bgfx/bgfx.h>

namespace game {

class Player;

class Zombie {
public:
    static constexpr engine::Collider k_collider{0.35f, 1.8f, 1.0f};

    Zombie(float x, float y, float z, engine::RiggedModel* model);

    void init_jolt(engine::JoltPhysics& jolt);
    void update(float dt, Player& player, engine::JoltPhysics& jolt);

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

    const engine::Collider& collider() const { return k_collider; }


    engine::CharHandle jolt_handle() const { return jolt_handle_; }

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

    engine::CharHandle        jolt_handle_ = engine::k_invalid_char;
    engine::CharVerticalState vert_state_{};

    engine::RiggedModel* model_;
    Animator             anim_;
};

} // namespace game
