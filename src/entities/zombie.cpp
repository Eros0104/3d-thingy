#include "entities/zombie.hpp"

#include "physics/jolt_physics.hpp"
#include "entities/player.hpp"

#include <cmath>
#include <cstdlib>

namespace game {

Zombie::Zombie(float x, float y, float z, engine::RiggedModel* model)
    : x(x), y(y), z(z), model_(model)
{
    if (model_) {
        anim_.bind(*model_);
        anim_.play(*model_, ZombieAnim::Idle, true, true);
    }
}

void Zombie::init_jolt(engine::JoltPhysics& jolt) {
    constexpr float k_step = 0.4f;
    jolt_handle_ = jolt.add_character(x, y, z,
        k_collider.radius, k_collider.height, k_step);
}

void Zombie::take_damage(int amount) {
    if (hp_ <= 0) return;
    hp_ -= amount;
    if (hp_ > 0 && model_ && std::rand() % 2 == 0) {
        anim_.play(*model_, ZombieAnim::GetHit, false, true);
        hit_stun_timer_ = 0.5f;
    }
}

void Zombie::capsule(float out_a[3], float out_b[3]) const {
    k_collider.capsule_endpoints(x, y, z, out_a, out_b);
}

void Zombie::update(float dt, game::Player& player, engine::JoltPhysics& jolt) {
    if (!model_ || !model_->valid()) return;

    constexpr float k_walk_speed      = 1.5f;
    constexpr float k_engage_dist     = 1.2f;
    constexpr float k_damage_interval = 1.0f;
    constexpr int   k_damage_per_hit  = 10;
    const float     k_min_sep         = k_collider.radius + Player::k_radius;

    if (!alive()) {
        if (!dying_) {
            dying_ = true;
            anim_.play(*model_, ZombieAnim::Death, false, true);
        }
        model_->update(dt);
        return;
    }

    FpsCamera& camera = player.camera();

    const float pdx     = camera.eyeX - x;
    const float pdz     = camera.eyeZ - z;
    const float dist_xz = std::sqrt(pdx * pdx + pdz * pdz);

    yaw = std::atan2(-pdx, pdz);

    if (hit_stun_timer_ > 0.f) {
        hit_stun_timer_ -= dt;
        if (hit_stun_timer_ < 0.f) hit_stun_timer_ = 0.f;

        // Still let gravity act even when stunned.
        jolt.move_character(jolt_handle_, vert_state_, 0.f, 0.f, dt);
        jolt.get_character_pos(jolt_handle_, x, y, z);
    } else if (dist_xz > k_engage_dist) {
        damage_timer_ = 0.f;
        const float inv   = 1.f / dist_xz;
        const float vel_x = pdx * inv * k_walk_speed;
        const float vel_z = pdz * inv * k_walk_speed;
        jolt.move_character(jolt_handle_, vert_state_, vel_x, vel_z, dt);
        jolt.get_character_pos(jolt_handle_, x, y, z);

        anim_.play(*model_, ZombieAnim::Walk, true, false);
    } else {
        damage_timer_ += dt;
        if (damage_timer_ >= k_damage_interval) {
            damage_timer_ -= k_damage_interval;
            player.take_damage(k_damage_per_hit);
        }
        if (anim_.current_z(*model_) != ZombieAnim::Attack || model_->current_finished())
            anim_.play_random(*model_, ZombieAnim::Attack, false);

        jolt.move_character(jolt_handle_, vert_state_, 0.f, 0.f, dt);
        jolt.get_character_pos(jolt_handle_, x, y, z);
    }



    model_->update(dt);
}

void Zombie::render(
    bgfx::ViewId view_id, bgfx::ProgramHandle program,
    bgfx::UniformHandle u_bones, bgfx::UniformHandle s_albedo,
    bgfx::UniformHandle u_baseColor, bgfx::TextureHandle fallback_white,
    uint64_t state) const
{
    if (!model_ || !model_->valid()) return;
    engine::CharacterDrawParams cdp{};
    cdp.pos[0] = x; cdp.pos[1] = y; cdp.pos[2] = z;
    cdp.yaw    = yaw;
    cdp.scale  = 1.f;
    model_->submit_world(view_id, program, u_bones, s_albedo,
                         u_baseColor, fallback_white, state, cdp);
}

} // namespace game
