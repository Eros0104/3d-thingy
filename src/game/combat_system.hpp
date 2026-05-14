#pragma once

#include "engine/physics/jolt_physics.hpp"
#include "game/particles.hpp"
#include "game/player.hpp"
#include "game/zombie.hpp"

#include <vector>

namespace game {

struct BulletHole {
    float x, y, z, nx, ny, nz;
};

class CombatSystem {
public:
    CombatSystem() = default;
    CombatSystem(engine::JoltPhysics& jolt, BloodParticleSystem& particles,
                 Player& player, std::vector<Zombie>& zombies);

    void update();

    const std::vector<BulletHole>& bullet_holes() const { return bullet_holes_; }

private:
    engine::JoltPhysics*   jolt_      = nullptr;
    BloodParticleSystem*   particles_ = nullptr;
    Player*                player_    = nullptr;
    std::vector<Zombie>*   zombies_   = nullptr;
    std::vector<BulletHole> bullet_holes_;
};

} // namespace game
