#include "game/zombie.hpp"

#include "game/level/level_data.hpp"
#include "game/physics_world.hpp"
#include "game/player.hpp"

#include <algorithm>
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

void Zombie::take_damage(int amount) {
    if (hp_ <= 0) return;
    hp_ -= amount;
    if (hp_ > 0 && model_ && std::rand() % 2 == 0) {
        anim_.play(*model_, ZombieAnim::GetHit, false, true);
        hit_stun_timer_ = 0.5f;
    }
}

void Zombie::capsule(float out_a[3], float out_b[3]) const {
    out_a[0] = x; out_a[1] = y + k_radius;             out_a[2] = z;
    out_b[0] = x; out_b[1] = y + k_height - k_radius;  out_b[2] = z;
}

void Zombie::update(float dt, game::Player& player, const engine::Level& level) {
    if (!model_ || !model_->valid()) return;

    constexpr float k_walk_speed      = 1.5f;
    constexpr float k_engage_dist     = 1.2f;
    constexpr float k_damage_interval = 1.0f;
    constexpr int   k_damage_per_hit  = 10;
    constexpr float k_min_sep         = k_radius + engine::PlayerPhysics::k_body_radius_xz;

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
    } else if (dist_xz > k_engage_dist) {
        damage_timer_ = 0.f;
        const float inv    = 1.0f / dist_xz;
        const float cand_x = x + pdx * inv * k_walk_speed * dt;
        const float cand_z = z + pdz * inv * k_walk_speed * dt;
        engine::resolve_xz_slide(level, x, z, cand_x, cand_z, y,
                                 k_radius, engine::PlayerPhysics::k_step_up, x, z);
        anim_.play(*model_, ZombieAnim::Walk, true, false);
    } else {
        damage_timer_ += dt;
        if (damage_timer_ >= k_damage_interval) {
            damage_timer_ -= k_damage_interval;
            player.take_damage(k_damage_per_hit);
        }
        if (anim_.current_z(*model_) != ZombieAnim::Attack || model_->current_finished())
            anim_.play_random(*model_, ZombieAnim::Attack, false);
    }

    // Push overlapping bodies apart.
    const float sep_dx   = x - camera.eyeX;
    const float sep_dz   = z - camera.eyeZ;
    const float sep_dist = std::sqrt(sep_dx * sep_dx + sep_dz * sep_dz);
    if (sep_dist > 0.f && sep_dist < k_min_sep) {
        const float half_push = 0.5f * (k_min_sep - sep_dist) / sep_dist;
        const float prev_x = x, prev_z = z;
        x           += sep_dx * half_push;
        z           += sep_dz * half_push;
        camera.eyeX -= sep_dx * half_push;
        camera.eyeZ -= sep_dz * half_push;
        engine::resolve_xz_slide(level, prev_x, prev_z, x, z, y,
                                 k_radius, engine::PlayerPhysics::k_step_up, x, z);
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
