#include "game/combat_system.hpp"

#include "engine/physics/raycast.hpp"
#include "game/fps_camera.hpp"

#include <limits>

namespace game {

CombatSystem::CombatSystem(engine::JoltPhysics& jolt, BloodParticleSystem& particles,
                           Player& player, std::vector<Zombie>& zombies)
    : jolt_(&jolt), particles_(&particles), player_(&player), zombies_(&zombies) {}

void CombatSystem::update() {
    float fx, fy, fz;
    if (!player_->try_fire(fx, fy, fz))
        return;

    const FpsCamera& cam = player_->camera();
    constexpr float k_max_ray = 500.f;

    engine::JoltRayHit jolt_hit;
    const bool wall_hit = jolt_->cast_ray_level(cam.eyeX, cam.eyeY, cam.eyeZ,
                                                fx, fy, fz, k_max_ray, jolt_hit);
    const float wall_t = jolt_hit.t;

    float best_t = wall_hit ? wall_t : std::numeric_limits<float>::infinity();

    for (auto& z : *zombies_) {
        if (!z.alive()) continue;
        float ca[3], cb[3];
        z.capsule(ca, cb);
        float t_hit;
        if (engine::ray_capsule(cam.eyeX, cam.eyeY, cam.eyeZ, fx, fy, fz,
                                ca[0], ca[1], ca[2], cb[0], cb[1], cb[2],
                                Zombie::k_collider.radius, t_hit) &&
            t_hit < best_t) {
            best_t = t_hit;
            z.take_damage(10);
            particles_->spawn(cam.eyeX + fx * t_hit, cam.eyeY + fy * t_hit,
                              cam.eyeZ + fz * t_hit, 25);
        }
    }

    if (wall_hit && best_t >= wall_t - 1e-4f) {
        constexpr float k_offset = 0.005f;
        constexpr int k_max = 200;
        bullet_holes_.push_back({
            cam.eyeX + fx * wall_t + jolt_hit.nx * k_offset,
            cam.eyeY + fy * wall_t + jolt_hit.ny * k_offset,
            cam.eyeZ + fz * wall_t + jolt_hit.nz * k_offset,
            jolt_hit.nx, jolt_hit.ny, jolt_hit.nz
        });
        if (int(bullet_holes_.size()) > k_max)
            bullet_holes_.erase(bullet_holes_.begin());
    }
}

} // namespace game
