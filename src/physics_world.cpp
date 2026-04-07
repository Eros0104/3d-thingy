#include "physics_world.hpp"

#include "fps_camera.hpp"

#include <algorithm>
#include <cmath>

namespace engine {

namespace {

bool over_platform_xy(float x, float z)
{
	return std::fabs(x) <= k_platform_half_extent && std::fabs(z) <= k_platform_half_extent;
}

} // namespace

void player_physics_step(FpsCamera& camera, PlayerPhysics& physics, float dt)
{
	physics.velocity_y -= PlayerPhysics::k_gravity * dt;
	camera.eyeY += physics.velocity_y * dt;

	const float feet_y = camera.eyeY - PlayerPhysics::k_eye_height;
	const bool over = over_platform_xy(camera.eyeX, camera.eyeZ);

	if (feet_y <= k_platform_surface_y && over && physics.velocity_y <= 0.0f) {
		camera.eyeY = k_platform_surface_y + PlayerPhysics::k_eye_height;
		physics.velocity_y = 0.0f;
	}

	const float feet_after = camera.eyeY - PlayerPhysics::k_eye_height;
	const float ground_eps = 0.08f;
	const bool on_slab = over
		&& feet_after <= k_platform_surface_y + ground_eps
		&& feet_after >= k_platform_surface_y - ground_eps;

	if (on_slab) {
		camera.eyeX = std::clamp(camera.eyeX, -k_platform_half_extent, k_platform_half_extent);
		camera.eyeZ = std::clamp(camera.eyeZ, -k_platform_half_extent, k_platform_half_extent);
	}
}

} // namespace engine
