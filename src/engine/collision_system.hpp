#pragma once

#include "engine/physics/jolt_physics.hpp"
#include "game/player.hpp"
#include "game/zombie.hpp"

namespace engine {
struct CollisionSystem {
public:
  CollisionSystem() = default;
  CollisionSystem(engine::JoltPhysics &jolt);
  ~CollisionSystem();

  void update();

  // TODO: create a generic entity instead of passing zombies and player
  void register_entity(game::Player*);

  void register_entity(game::Zombie*);

  void toggle_debug_mode();

  bool is_debug_mode();

private:
  bool debug_mode = false;
  engine::JoltPhysics *jolt_ = nullptr;
  std::vector<game::Zombie *> zombies_;
  game::Player *player_ = nullptr;
};
} // namespace engine
