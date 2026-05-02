#pragma once

struct FpsCamera;

namespace engine {

struct Level;

struct PlayerPhysics {
	float velocity_y = 0.0f;
	static constexpr float k_gravity = 28.0f;
	static constexpr float k_eye_height = 1.6f;
	/// Horizontal clearance vs walls so the eye never sits inside wall geometry.
	static constexpr float k_body_radius_xz = 0.28f;
	/// Max step the player can snap up without jumping (stairs, small ledges).
	static constexpr float k_step_up = 1.0f;
};

/// After WASD, `prev_eye_x`/`prev_eye_z` are the position before that move. Applies gravity,
/// resolves wall segment collisions, and lands on the highest reachable sector/stair surface.
void player_physics_step(
	FpsCamera& camera,
	PlayerPhysics& physics,
	float dt,
	const Level& level,
	float prev_eye_x,
	float prev_eye_z
);

/// Resolves candidate XZ movement against walls and cliffs with slide fallback.
/// Use for any body that moves horizontally (NPCs, etc.).
void resolve_xz_slide(
	const Level& level,
	float prev_x, float prev_z,
	float cand_x, float cand_z,
	float feet_y, float radius, float step_up,
	float& out_x, float& out_z
);

} // namespace engine
