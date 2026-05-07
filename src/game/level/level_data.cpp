#include "game/level/level_data.hpp"

namespace engine {

float polygon_signed_area(const std::vector<Vec2>& poly)
{
	const size_t n = poly.size();
	if (n < 3) {
		return 0.0f;
	}
	float a = 0.0f;
	for (size_t i = 0; i < n; ++i) {
		const Vec2& p = poly[i];
		const Vec2& q = poly[(i + 1) % n];
		a += p.x * q.z - q.x * p.z;
	}
	return 0.5f * a;
}

bool point_in_polygon(const std::vector<Vec2>& poly, Vec2 p)
{
	const size_t n = poly.size();
	if (n < 3) {
		return false;
	}
	bool inside = false;
	for (size_t i = 0, j = n - 1; i < n; j = i++) {
		const Vec2& a = poly[i];
		const Vec2& b = poly[j];
		const bool crosses_y = (a.z > p.z) != (b.z > p.z);
		if (!crosses_y) {
			continue;
		}
		const float x_at = (b.x - a.x) * (p.z - a.z) / (b.z - a.z) + a.x;
		if (p.x < x_at) {
			inside = !inside;
		}
	}
	return inside;
}

} // namespace engine
