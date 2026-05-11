#pragma once

#include "engine/physics/jolt_physics.hpp"
#include "game/player.hpp"
#include "game/zombie.hpp"

#include <bgfx/bgfx.h>

namespace engine {
struct CollisionSystem {
public:
  CollisionSystem() = default;
  CollisionSystem(engine::JoltPhysics &jolt, bgfx::ProgramHandle debug_prog);
  ~CollisionSystem();

  void update();
  void render(bgfx::ViewId view_id, bgfx::ProgramHandle prog);

  // TODO: create a generic entity instead of passing zombies and player
  void register_entity(game::Player*);

  void register_entity(game::Zombie*);

  void render(bgfx::ViewId view_id);

  void toggle_debug_mode();

  bool is_debug_mode();

private:
  bool debug_mode = false;
  engine::JoltPhysics *jolt_ = nullptr;
  bgfx::ProgramHandle debug_prog_ = BGFX_INVALID_HANDLE;
  std::vector<game::Zombie *> zombies_;
  game::Player *player_ = nullptr;
};
} // namespace engine
