#include "game/level/level_data.hpp"

namespace engine {

bool wall_type_from_string(const std::string& s, WallType& out)
{
	if (s == "normal" || s == "normal_wall" || s == "solid") {
		out = WallType::Normal;
		return true;
	}
	if (s == "door" || s == "door_wall") {
		out = WallType::Door;
		return true;
	}
	if (s == "broken" || s == "broken_wall") {
		out = WallType::Broken;
		return true;
	}
	if (s == "window" || s == "window_wall") {
		out = WallType::Window;
		return true;
	}
	return false;
}

const char* wall_type_to_string(WallType t)
{
	switch (t) {
	case WallType::Normal: return "normal";
	case WallType::Door: return "door";
	case WallType::Broken: return "broken";
	case WallType::Window: return "window";
	}
	return "normal";
}

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
