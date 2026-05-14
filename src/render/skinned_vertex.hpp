#pragma once

#include <cstdint>

struct SkinnedVertex {
	float x = 0.0f, y = 0.0f, z = 0.0f;
	float nx = 0.0f, ny = 1.0f, nz = 0.0f;
	float u = 0.0f, v = 0.0f;
	uint8_t joints[4] = {0, 0, 0, 0};
	float weights[4] = {1.0f, 0.0f, 0.0f, 0.0f};
};
