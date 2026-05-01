#pragma once

#include <cstdint>

/// First-person camera: yaw around Y, pitch around X (radians). +Z is forward at yaw=0.
struct FpsCamera {
	float eyeX = 0.0f;
	float eyeY = 1.6f;
	float eyeZ = -3.0f;
	float yaw = 0.0f;
	float pitch = 0.0f;
};

void fps_camera_apply_mouse(FpsCamera& camera, float relX, float relY, float sensitivity);

void fps_camera_apply_wasd(
	const FpsCamera& camera,
	const uint8_t* keys,
	float dt,
	float moveSpeed,
	float& outDeltaX,
	float& outDeltaY,
	float& outDeltaZ
);

/// Writes row-major 4x4 view and projection (bgfx/bx layout).
void fps_camera_view_proj(
	const FpsCamera& camera,
	float aspect,
	bool homogeneousDepth,
	float outView[16],
	float outProj[16]
);
