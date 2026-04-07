#include "fps_camera.hpp"

#include <SDL.h>

#include <bx/math.h>

#include <cmath>

void fps_camera_apply_mouse(FpsCamera& camera, float relX, float relY, float sensitivity)
{
	camera.yaw += relX * sensitivity;
	camera.pitch -= relY * sensitivity;

	const float pitchLimit = bx::kPiHalf - 0.02f;
	if (camera.pitch > pitchLimit) {
		camera.pitch = pitchLimit;
	}
	if (camera.pitch < -pitchLimit) {
		camera.pitch = -pitchLimit;
	}
}

void fps_camera_apply_wasd(
	const FpsCamera& camera,
	const uint8_t* keys,
	float dt,
	float moveSpeed,
	float& outDeltaX,
	float& outDeltaY,
	float& outDeltaZ
)
{
	const float fx = std::cos(camera.pitch) * std::sin(camera.yaw);
	const float fy = std::sin(camera.pitch);
	const float fz = std::cos(camera.pitch) * std::cos(camera.yaw);
	const bx::Vec3 forward = { fx, fy, fz };
	const bx::Vec3 worldUp = { 0.0f, 1.0f, 0.0f };

	bx::Vec3 forwardFlat = { forward.x, 0.0f, forward.z };
	const float flatLenSq = forwardFlat.x * forwardFlat.x + forwardFlat.z * forwardFlat.z;
	if (flatLenSq < 1e-8f) {
		forwardFlat.x = std::sin(camera.yaw);
		forwardFlat.z = std::cos(camera.yaw);
	} else {
		const float inv = 1.0f / std::sqrt(flatLenSq);
		forwardFlat.x *= inv;
		forwardFlat.z *= inv;
	}

	const bx::Vec3 rightFlat = bx::normalize(bx::cross(worldUp, forwardFlat));

	bx::Vec3 move = { 0.0f, 0.0f, 0.0f };
	if (keys[SDL_SCANCODE_W]) {
		move = bx::add(move, forwardFlat);
	}
	if (keys[SDL_SCANCODE_S]) {
		move = bx::sub(move, forwardFlat);
	}
	if (keys[SDL_SCANCODE_D]) {
		move = bx::add(move, rightFlat);
	}
	if (keys[SDL_SCANCODE_A]) {
		move = bx::sub(move, rightFlat);
	}

	const float len = bx::length(move);
	if (len > 1e-6f) {
		move = bx::mul(bx::normalize(move), moveSpeed * dt);
	}

	outDeltaX = move.x;
	outDeltaY = move.y;
	outDeltaZ = move.z;
}

void fps_camera_view_proj(
	const FpsCamera& camera,
	float aspect,
	bool homogeneousDepth,
	float outView[16],
	float outProj[16]
)
{
	const float fx = std::cos(camera.pitch) * std::sin(camera.yaw);
	const float fy = std::sin(camera.pitch);
	const float fz = std::cos(camera.pitch) * std::cos(camera.yaw);
	const bx::Vec3 forward = { fx, fy, fz };
	const bx::Vec3 eye = { camera.eyeX, camera.eyeY, camera.eyeZ };
	const bx::Vec3 at = bx::add(eye, forward);
	const bx::Vec3 up = { 0.0f, 1.0f, 0.0f };

	bx::mtxLookAt(outView, eye, at, up);
	bx::mtxProj(outProj, 70.0f, aspect, 0.1f, 200.0f, homogeneousDepth);
}
