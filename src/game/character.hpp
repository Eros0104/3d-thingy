#pragma once

#include "game/viewmodel.hpp"

namespace engine {

struct Character {
	Viewmodel model;

	float x = 0.0f;
	float y = 0.0f; // feet (floor level)
	float z = 0.0f;
	float yaw = 0.0f;
	int hp = 100;

	static constexpr float k_radius = 0.35f;
	static constexpr float k_height = 1.8f;

	bool alive() const { return hp > 0; }

	// Segment endpoints of the capsule (sphere centers at base and tip).
	void capsule_segment(float out_a[3], float out_b[3]) const {
		out_a[0] = x; out_a[1] = y + k_radius;          out_a[2] = z;
		out_b[0] = x; out_b[1] = y + k_height - k_radius; out_b[2] = z;
	}
};

} // namespace engine
