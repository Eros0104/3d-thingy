#pragma once

namespace game { class Player; }
class Door;

namespace game {

class InteractSystem {
public:
    InteractSystem() = default;
    InteractSystem(Player& player, Door& door, float door_x, float door_z);

    void on_interact();
    void update();

    bool near_door() const { return near_door_; }

private:
    Player* player_           = nullptr;
    Door*   door_             = nullptr;
    float   door_x_           = 0.f;
    float   door_z_           = 0.f;
    bool    interact_pressed_ = false;
    bool    near_door_        = false;
};

} // namespace game
