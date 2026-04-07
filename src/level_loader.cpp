#include "level_loader.hpp"
#include "physics_world.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace engine {

namespace {

static constexpr float k_floor_uv_per_world = 1.0f / 6.0f;
static constexpr float k_wall_uv_scale = 0.35f;
static constexpr uint32_t k_floor_abgr = 0xffffffffu;
static constexpr uint32_t k_wall_abgr = 0xffe8e4ddu;

std::string read_text_file(const char* path, std::string& err)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) {
		err = std::string("cannot open: ") + path;
		return {};
	}
	std::ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

std::vector<std::string> split_nonempty_lines(const std::string& text)
{
	std::vector<std::string> lines;
	std::istringstream stream(text);
	std::string line;
	while (std::getline(stream, line)) {
		while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
			line.pop_back();
		}
		if (!line.empty()) {
			lines.push_back(line);
		}
	}
	return lines;
}

bool parse_grid(const std::vector<std::string>& lines, std::vector<char>& out_tiles, int& out_w, int& out_h, std::string& err)
{
	if (lines.empty()) {
		err = "empty map";
		return false;
	}
	out_w = static_cast<int>(lines[0].size());
	for (const std::string& row : lines) {
		if (static_cast<int>(row.size()) != out_w) {
			err = "map rows must have equal width";
			return false;
		}
		for (char ch : row) {
			if (ch != 'x' && ch != '#') {
				err = std::string("invalid map character: ") + ch;
				return false;
			}
		}
	}
	out_h = static_cast<int>(lines.size());
	out_tiles.resize(static_cast<size_t>(out_w * out_h));
	for (int r = 0; r < out_h; ++r) {
		for (int c = 0; c < out_w; ++c) {
			out_tiles[static_cast<size_t>(r * out_w + c)] = lines[static_cast<size_t>(r)][static_cast<size_t>(c)];
		}
	}
	return true;
}

void append_floor_cell(
	std::vector<LitVertex>& v,
	float x0,
	float z0,
	float x1,
	float z1,
	float y
)
{
	const float nx = 0.0f;
	const float ny = 1.0f;
	const float nz = 0.0f;
	auto vtx = [&](float px, float pz, float u, float fv) {
		LitVertex vert;
		vert.x = px;
		vert.y = y;
		vert.z = pz;
		vert.nx = nx;
		vert.ny = ny;
		vert.nz = nz;
		vert.u = u * k_floor_uv_per_world;
		vert.v = fv * k_floor_uv_per_world;
		vert.abgr = k_floor_abgr;
		v.push_back(vert);
	};
	vtx(x0, z0, x0, z0);
	vtx(x1, z1, x1, z1);
	vtx(x1, z0, x1, z0);
	vtx(x0, z0, x0, z0);
	vtx(x0, z1, x0, z1);
	vtx(x1, z1, x1, z1);
}

void append_box(
	std::vector<LitVertex>& v,
	float cx,
	float cy,
	float cz,
	float hx,
	float hy,
	float hz
)
{
	const float x0 = cx - hx;
	const float x1 = cx + hx;
	const float y0 = cy - hy;
	const float y1 = cy + hy;
	const float z0 = cz - hz;
	const float z1 = cz + hz;

	auto face = [&](float nx, float ny, float nz,
		float ax, float ay, float az,
		float bx, float by, float bz,
		float cx2, float cy2, float cz2,
		float dx, float dy, float dz) {
		auto corner = [&](float px, float py, float pz) {
			LitVertex vert;
			vert.x = px;
			vert.y = py;
			vert.z = pz;
			vert.nx = nx;
			vert.ny = ny;
			vert.nz = nz;
			vert.u = (px + pz) * k_wall_uv_scale;
			vert.v = (py + pz) * k_wall_uv_scale;
			vert.abgr = k_wall_abgr;
			v.push_back(vert);
		};
		corner(ax, ay, az);
		corner(bx, by, bz);
		corner(cx2, cy2, cz2);
		corner(ax, ay, az);
		corner(cx2, cy2, cz2);
		corner(dx, dy, dz);
	};

	face(0.0f, 0.0f, -1.0f, x0, y0, z0, x1, y0, z0, x1, y1, z0, x0, y1, z0);
	face(0.0f, 0.0f, 1.0f, x1, y0, z1, x0, y0, z1, x0, y1, z1, x1, y1, z1);
	face(-1.0f, 0.0f, 0.0f, x0, y0, z1, x0, y0, z0, x0, y1, z0, x0, y1, z1);
	face(1.0f, 0.0f, 0.0f, x1, y0, z0, x1, y0, z1, x1, y1, z1, x1, y1, z0);
	face(0.0f, -1.0f, 0.0f, x0, y0, z1, x1, y0, z1, x1, y0, z0, x0, y0, z0);
	face(0.0f, 1.0f, 0.0f, x0, y1, z0, x1, y1, z0, x1, y1, z1, x0, y1, z1);
}

} // namespace

void LoadedLevel::world_to_cell(float wx, float wz, int& c, int& r) const
{
	const float ox = static_cast<float>(width) * cell_size * 0.5f;
	const float oz = static_cast<float>(height) * cell_size * 0.5f;
	c = static_cast<int>(std::floor((wx + ox) / cell_size));
	r = static_cast<int>(std::floor((wz + oz) / cell_size));
}

void LoadedLevel::cell_center_world(int col, int row, float& out_x, float& out_z) const
{
	const float ox = static_cast<float>(width) * cell_size * 0.5f;
	const float oz = static_cast<float>(height) * cell_size * 0.5f;
	out_x = (static_cast<float>(col) + 0.5f) * cell_size - ox;
	out_z = (static_cast<float>(row) + 0.5f) * cell_size - oz;
}

bool LoadedLevel::walkable_at_world(float wx, float wz) const
{
	int c = 0;
	int r = 0;
	world_to_cell(wx, wz, c, r);
	return is_floor(c, r);
}

bool load_evil_level(const char* map_path, const char* lights_path, LoadedLevel& out, std::string& err)
{
	out = LoadedLevel{};
	std::string map_text = read_text_file(map_path, err);
	if (map_text.empty() && !err.empty()) {
		return false;
	}
	if (map_text.empty()) {
		err = "empty map file";
		return false;
	}

	std::vector<std::string> map_lines = split_nonempty_lines(map_text);
	std::vector<char> tiles;
	int w = 0;
	int h = 0;
	if (!parse_grid(map_lines, tiles, w, h, err)) {
		return false;
	}
	out.width = w;
	out.height = h;
	out.tiles = std::move(tiles);

	if (lights_path != nullptr && lights_path[0] != '\0') {
		std::string light_err;
		std::string lights_text = read_text_file(lights_path, light_err);
		if (lights_text.empty()) {
			err = light_err.empty() ? "empty lights file" : light_err;
			return false;
		}
		std::vector<std::string> light_lines = split_nonempty_lines(lights_text);
		if (static_cast<int>(light_lines.size()) != out.height) {
			err = "lights file row count must match map";
			return false;
		}
		for (int r = 0; r < out.height; ++r) {
			if (static_cast<int>(light_lines[static_cast<size_t>(r)].size()) != out.width) {
				err = "lights file column count must match map";
				return false;
			}
			for (int c = 0; c < out.width; ++c) {
				const char ch = light_lines[static_cast<size_t>(r)][static_cast<size_t>(c)];
				if (ch != '.' && ch != '*') {
					err = "lights file may only contain '.' or '*'";
					return false;
				}
				if (ch == '*') {
					if (!out.is_floor(c, r)) {
						std::fprintf(stderr, "evil: light at (%d,%d) ignored (not floor)\n", c, r);
						continue;
					}
					float wx = 0.0f;
					float wz = 0.0f;
					out.cell_center_world(c, r, wx, wz);
					out.light_positions.push_back(wx);
					out.light_positions.push_back(3.2f);
					out.light_positions.push_back(wz);
				}
			}
		}
	}

	return true;
}

void build_level_meshes(
	const LoadedLevel& level,
	float wall_height,
	std::vector<LitVertex>& out_floor_vertices,
	std::vector<LitVertex>& out_wall_vertices
)
{
	out_floor_vertices.clear();
	out_wall_vertices.clear();
	const float ox = static_cast<float>(level.width) * level.cell_size * 0.5f;
	const float oz = static_cast<float>(level.height) * level.cell_size * 0.5f;
	const float cs = level.cell_size;
	const float half = cs * 0.5f;
	const float wh2 = wall_height * 0.5f;

	for (int r = 0; r < level.height; ++r) {
		for (int c = 0; c < level.width; ++c) {
			const float x0 = static_cast<float>(c) * cs - ox;
			const float x1 = static_cast<float>(c + 1) * cs - ox;
			const float z0 = static_cast<float>(r) * cs - oz;
			const float z1 = static_cast<float>(r + 1) * cs - oz;

			if (level.is_floor(c, r)) {
				append_floor_cell(out_floor_vertices, x0, z0, x1, z1, engine::k_platform_surface_y);
			} else if (level.is_wall(c, r)) {
				const float cx = (x0 + x1) * 0.5f;
				const float cz = (z0 + z1) * 0.5f;
				const float cy = wh2;
				append_box(out_wall_vertices, cx, cy, cz, half, wh2, half);
			}
		}
	}
}

} // namespace engine
