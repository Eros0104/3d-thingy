#include "ecs.hpp"

namespace engine {

void ecs_bootstrap(Registry& registry)
{
	const auto entity = registry.create();
	registry.emplace<Position>(entity, 0.0f, 0.0f, 0.0f);
	(void)entity;
}

} // namespace engine
