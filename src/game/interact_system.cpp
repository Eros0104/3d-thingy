#include "game/interact_system.hpp"

#include "game/door.hpp"
#include "game/fps_camera.hpp"
#include "game/player.hpp"

namespace game {

InteractSystem::InteractSystem(Player& player, Door& door, float door_x, float door_z)
    : player_(&player), door_(&door), door_x_(door_x), door_z_(door_z) {}

void InteractSystem::on_interact() { interact_pressed_ = true; }

void InteractSystem::update() {
    constexpr float k_interact_radius = 2.0f;
    const FpsCamera& cam = player_->camera();
    const float dx = cam.eyeX - door_x_;
    const float dz = cam.eyeZ - door_z_;
    near_door_ = (dx * dx + dz * dz) < (k_interact_radius * k_interact_radius);

    if (interact_pressed_ && near_door_)
        door_->is_open() ? door_->close() : door_->open();

    interact_pressed_ = false;
}

} // namespace game
