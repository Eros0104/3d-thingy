#include "engine/collision_system.hpp"
#include "engine/physics/jolt_physics.hpp"
#include "game/fps_camera.hpp"

namespace engine {

CollisionSystem::CollisionSystem(engine::JoltPhysics &jolt) { jolt_ = &jolt; }

CollisionSystem::~CollisionSystem() = default;

void CollisionSystem::update() {
  for (auto z : zombies_) {
   FpsCamera p_cam = player_->camera();

    // Push overlapping bodies apart (CharacterVirtual doesn't self-separate).
    const float sep_dx   = z->x - p_cam.eyeX;
    const float sep_dz   = z->z - p_cam.eyeZ;
    const float sep_dist = std::sqrt(sep_dx * sep_dx + sep_dz * sep_dz);
    const float     k_min_sep         = z->k_collider.radius + player_->k_radius;

    if (sep_dist > 0.f && sep_dist < k_min_sep) {
        const float half_push = 0.5f * (k_min_sep - sep_dist) / sep_dist;

        z->x        += sep_dx * half_push;
        z->z        += sep_dz * half_push;
        p_cam.eyeX -= sep_dx * half_push;
        p_cam.eyeZ -= sep_dz * half_push;

        // Sync teleported positions back into Jolt.
        jolt_->set_character_pos(z->jolt_handle(), z->x, z->y, z->z);

        float px, py, pz;
        jolt_->get_character_pos(player_->jolt_handle(), px, py, pz);
        jolt_->set_character_pos(player_->jolt_handle(), p_cam.eyeX, py, p_cam.eyeZ);
    }
  }
}

void CollisionSystem::register_entity(game::Player* p) {
  player_ = p;
}

void CollisionSystem::register_entity(game::Zombie* z) {
  zombies_.push_back(z);
}

bool CollisionSystem::is_debug_mode() { return debug_mode; }

void CollisionSystem::toggle_debug_mode() { debug_mode = !debug_mode; }

} // namespace engine
