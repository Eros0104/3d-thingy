#include "game/level/json_level.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace engine {

namespace {

using json = nlohmann::json;

bool read_file(const char* path, std::string& out, std::string& err)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) {
		err = std::string("cannot open: ") + path;
		return false;
	}
	std::ostringstream ss;
	ss << f.rdbuf();
	out = ss.str();
	return true;
}

bool as_float(const json& v, float& out)
{
	if (!v.is_number()) {
		return false;
	}
	out = v.get<float>();
	return true;
}

bool get_float(const json& j, const char* key, float& out)
{
	if (!j.contains(key)) {
		return false;
	}
	return as_float(j.at(key), out);
}

bool get_int(const json& j, const char* key, int& out)
{
	if (!j.contains(key)) {
		return false;
	}
	const json& v = j.at(key);
	if (!v.is_number_integer() && !v.is_number_unsigned()) {
		return false;
	}
	out = v.get<int>();
	return true;
}

bool parse_vec2(const json& v, Vec2& out)
{
	if (!v.is_array() || v.size() != 2) {
		return false;
	}
	if (!as_float(v[0], out.x) || !as_float(v[1], out.z)) {
		return false;
	}
	return true;
}

bool parse_vec3(const json& v, Vec3& out)
{
	if (!v.is_array() || v.size() != 3) {
		return false;
	}
	if (!as_float(v[0], out.x) || !as_float(v[1], out.y) || !as_float(v[2], out.z)) {
		return false;
	}
	return true;
}

bool parse_color(const json& v, std::array<float, 3>& out)
{
	if (!v.is_array() || v.size() != 3) {
		return false;
	}
	for (size_t i = 0; i < 3; ++i) {
		if (!as_float(v[i], out[i])) {
			return false;
		}
	}
	return true;
}

bool parse_sector(const json& js, float default_wall_height, Sector& out, std::string& err)
{
	if (js.contains("id") && js.at("id").is_string()) {
		out.id = js.at("id").get<std::string>();
	}
	if (!js.contains("polygon") || !js.at("polygon").is_array()) {
		err = "sector missing 'polygon' array";
		return false;
	}
	const json& poly = js.at("polygon");
	if (poly.size() < 3) {
		err = "sector polygon must have >= 3 points";
		return false;
	}
	out.polygon.reserve(poly.size());
	for (size_t i = 0; i < poly.size(); ++i) {
		Vec2 p;
		if (!parse_vec2(poly[i], p)) {
			err = std::string("sector polygon[") + std::to_string(i) + "] must be [x, z]";
			return false;
		}
		out.polygon.push_back(p);
	}
	out.floor_y = 0.0f;
	out.ceiling_y = default_wall_height;
	get_float(js, "floor_y", out.floor_y);
	get_float(js, "ceiling_y", out.ceiling_y);
	if (out.ceiling_y <= out.floor_y) {
		err = "sector ceiling_y must be > floor_y";
		return false;
	}
	if (polygon_signed_area(out.polygon) < 0.0f) {
		std::reverse(out.polygon.begin(), out.polygon.end());
	}
	return true;
}

bool parse_wall(const json& jw, float default_wall_height, Wall& out, std::string& err)
{
	std::string type_s = "normal";
	if (jw.contains("type") && jw.at("type").is_string()) {
		type_s = jw.at("type").get<std::string>();
	}
	if (!wall_type_from_string(type_s, out.type)) {
		err = std::string("unknown wall type '") + type_s + "'";
		return false;
	}
	if (!jw.contains("a") || !jw.contains("b")) {
		err = "wall missing 'a' or 'b'";
		return false;
	}
	if (!parse_vec2(jw.at("a"), out.a) || !parse_vec2(jw.at("b"), out.b)) {
		err = "wall 'a'/'b' must be [x, z]";
		return false;
	}
	out.y0 = 0.0f;
	out.y1 = default_wall_height;
	get_float(jw, "y0", out.y0);
	get_float(jw, "y1", out.y1);
	if (out.y1 <= out.y0) {
		err = "wall y1 must be > y0";
		return false;
	}
	out.thickness = 0.2f;
	get_float(jw, "thickness", out.thickness);
	if (out.thickness < 0.0f) out.thickness = 0.0f;

	out.door_width = 1.2f;
	out.door_offset = -1.0f;
	out.door_height = 2.2f;
	get_float(jw, "door_width", out.door_width);
	get_float(jw, "door_offset", out.door_offset);
	get_float(jw, "door_height", out.door_height);
	return true;
}

bool parse_stair(const json& js, Stair& out, std::string& err)
{
	if (!js.contains("center_a") || !js.contains("center_b")) {
		err = "stair missing 'center_a' or 'center_b'";
		return false;
	}
	if (!parse_vec2(js.at("center_a"), out.center_a) || !parse_vec2(js.at("center_b"), out.center_b)) {
		err = "stair 'center_a'/'center_b' must be [x, z]";
		return false;
	}
	out.width = 2.0f;
	out.from_y = 0.0f;
	out.to_y = 3.2f;
	out.steps = 8;
	get_float(js, "width", out.width);
	get_float(js, "from_y", out.from_y);
	get_float(js, "to_y", out.to_y);
	get_int(js, "steps", out.steps);
	if (out.width <= 0.0f) {
		err = "stair width must be > 0";
		return false;
	}
	const float dx = out.center_b.x - out.center_a.x;
	const float dz = out.center_b.z - out.center_a.z;
	if (dx * dx + dz * dz <= 1e-6f) {
		err = "stair center_a and center_b must differ";
		return false;
	}
	if (out.steps < 1) {
		out.steps = 1;
	}
	return true;
}

bool parse_light(const json& jl, Light& out, std::string& err)
{
	if (!jl.contains("pos") || !parse_vec3(jl.at("pos"), out.pos)) {
		err = "light missing valid 'pos' [x, y, z]";
		return false;
	}
	if (jl.contains("color") && !parse_color(jl.at("color"), out.color)) {
		err = "light 'color' must be [r, g, b]";
		return false;
	}
	out.intensity = 1.0f;
	get_float(jl, "intensity", out.intensity);
	return true;
}

} // namespace

bool parse_json_level(const std::string& text, Level& out, std::string& err)
{
	out = Level{};
	json j;
	try {
		j = json::parse(text);
	} catch (const std::exception& e) {
		err = std::string("json parse error: ") + e.what();
		return false;
	}
	if (!j.is_object()) {
		err = "top-level must be an object";
		return false;
	}

	get_int(j, "version", out.version);
	if (j.contains("name") && j.at("name").is_string()) {
		out.name = j.at("name").get<std::string>();
	}
	get_float(j, "wall_height", out.wall_height);
	if (j.contains("ambient") && !parse_color(j.at("ambient"), out.ambient)) {
		err = "'ambient' must be [r, g, b]";
		return false;
	}

	if (j.contains("spawn")) {
		const json& js = j.at("spawn");
		if (!js.contains("pos") || !parse_vec3(js.at("pos"), out.spawn.pos)) {
			err = "'spawn.pos' must be [x, y, z]";
			return false;
		}
		get_float(js, "yaw_deg", out.spawn.yaw_deg);
	}

	if (j.contains("sectors") && j.at("sectors").is_array()) {
		const json& jss = j.at("sectors");
		out.sectors.reserve(jss.size());
		for (size_t i = 0; i < jss.size(); ++i) {
			Sector s;
			std::string serr;
			if (!parse_sector(jss[i], out.wall_height, s, serr)) {
				err = std::string("sectors[") + std::to_string(i) + "]: " + serr;
				return false;
			}
			out.sectors.push_back(std::move(s));
		}
	}

	if (j.contains("walls") && j.at("walls").is_array()) {
		const json& jws = j.at("walls");
		out.walls.reserve(jws.size());
		for (size_t i = 0; i < jws.size(); ++i) {
			Wall w;
			std::string werr;
			if (!parse_wall(jws[i], out.wall_height, w, werr)) {
				err = std::string("walls[") + std::to_string(i) + "]: " + werr;
				return false;
			}
			out.walls.push_back(w);
		}
	}

	if (j.contains("stairs") && j.at("stairs").is_array()) {
		const json& jss = j.at("stairs");
		out.stairs.reserve(jss.size());
		for (size_t i = 0; i < jss.size(); ++i) {
			Stair s;
			std::string serr;
			if (!parse_stair(jss[i], s, serr)) {
				err = std::string("stairs[") + std::to_string(i) + "]: " + serr;
				return false;
			}
			out.stairs.push_back(s);
		}
	}

	if (j.contains("lights") && j.at("lights").is_array()) {
		const json& jls = j.at("lights");
		out.lights.reserve(jls.size());
		for (size_t i = 0; i < jls.size(); ++i) {
			Light l;
			std::string lerr;
			if (!parse_light(jls[i], l, lerr)) {
				err = std::string("lights[") + std::to_string(i) + "]: " + lerr;
				return false;
			}
			out.lights.push_back(l);
		}
	}

	return true;
}

bool load_json_level(const char* path, Level& out, std::string& err)
{
	std::string text;
	if (!read_file(path, text, err)) {
		return false;
	}
	return parse_json_level(text, out, err);
}

} // namespace engine
