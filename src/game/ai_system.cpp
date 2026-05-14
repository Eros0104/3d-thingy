#include "game/ai_system.hpp"

namespace game {

AISystem::AISystem(engine::JoltPhysics& jolt, Player& player, std::vector<Zombie>& zombies)
    : jolt_(&jolt), player_(&player), zombies_(&zombies) {}

void AISystem::update(float dt) {
    for (auto& z : *zombies_)
        z.update(dt, *player_, *jolt_);
}

} // namespace game
