#pragma once

#include <entt/entt.hpp>

namespace engine {

using Registry = entt::registry;

struct Position {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

/// Reserved for wiring physics to ECS (currently `PlayerPhysics` in main owns vertical motion).
struct Velocity {
	float vx = 0.0f;
	float vy = 0.0f;
	float vz = 0.0f;
};

void ecs_bootstrap(Registry& registry);

} // namespace engine
