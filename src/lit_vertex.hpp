#pragma once

#include <cstdint>

struct LitVertex {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float nx = 0.0f;
	float ny = 1.0f;
	float nz = 0.0f;
	float u = 0.0f;
	float v = 0.0f;
	uint32_t abgr = 0xffffffff;
};
