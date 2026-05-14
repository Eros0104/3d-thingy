#pragma once

#include "physics/jolt_physics.hpp"
#include "entities/player.hpp"
#include "entities/zombie.hpp"

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
