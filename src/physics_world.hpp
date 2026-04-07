#pragma once

struct FpsCamera;

namespace engine {

struct LoadedLevel;

constexpr float k_platform_surface_y = 0.0f;

/// Eye height above feet (horizontal contact point at feet).
struct PlayerPhysics {
	float velocity_y = 0.0f;
	static constexpr float k_gravity = 28.0f;
	static constexpr float k_eye_height = 1.6f;
	/// Horizontal clearance vs walls so the eye never sits inside wall geometry (see-through).
	static constexpr float k_body_radius_xz = 0.28f;
};

/// After WASD, `prev_eye_x`/`prev_eye_z` are the position before that move. Gravity, landing on
/// `#` cells, and sliding off walls (revert horizontal move if the eye sits in a wall cell).
void player_physics_step(
	FpsCamera& camera,
	PlayerPhysics& physics,
	float dt,
	const LoadedLevel& level,
	float prev_eye_x,
	float prev_eye_z
);

} // namespace engine
