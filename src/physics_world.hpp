#pragma once

struct FpsCamera;

namespace engine {

/// Must match the walkable quad in `main.cpp` (collision uses the same bounds).
constexpr float k_platform_half_extent = 24.0f;
constexpr float k_platform_surface_y = 0.0f;

/// Eye height above feet (horizontal contact point at feet).
struct PlayerPhysics {
	float velocity_y = 0.0f;
	static constexpr float k_gravity = 28.0f;
	static constexpr float k_eye_height = 1.6f;
};

/// Horizontal move (WASD) is applied before this. Integrates gravity, landing on the
/// platform slab (|x|,|z| <= half_extent, top y = k_platform_surface_y), and clamps XZ on the slab.
void player_physics_step(FpsCamera& camera, PlayerPhysics& physics, float dt);

} // namespace engine
