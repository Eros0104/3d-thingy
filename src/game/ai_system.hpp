#pragma once

#include "engine/physics/jolt_physics.hpp"
#include "game/player.hpp"
#include "game/zombie.hpp"

#include <vector>

namespace game {

class AISystem {
public:
    AISystem() = default;
    AISystem(engine::JoltPhysics& jolt, Player& player, std::vector<Zombie>& zombies);

    void update(float dt);

private:
    engine::JoltPhysics* jolt_   = nullptr;
    Player*              player_ = nullptr;
    std::vector<Zombie>* zombies_ = nullptr;
};

} // namespace game
