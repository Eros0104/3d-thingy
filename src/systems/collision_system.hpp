#pragma once

#include "physics/jolt_physics.hpp"
#include "entities/player.hpp"
#include "entities/zombie.hpp"

#include <bgfx/bgfx.h>
#include <vector>

namespace engine {

struct CollisionSystem {
public:
    CollisionSystem() = default;
    CollisionSystem(engine::JoltPhysics& jolt, bgfx::ProgramHandle debug_prog,
                    game::Player& player, std::vector<game::Zombie>& zombies);
    ~CollisionSystem();

    void update();
    void render(bgfx::ViewId view_id);

    void toggle_debug_mode();
    bool is_debug_mode();

private:
    bool                  debug_mode  = false;
    engine::JoltPhysics*  jolt_       = nullptr;
    bgfx::ProgramHandle   debug_prog_ = BGFX_INVALID_HANDLE;
    game::Player*         player_     = nullptr;
    std::vector<game::Zombie>* zombies_ = nullptr;
};

} // namespace engine
