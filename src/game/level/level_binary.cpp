#include "game/level/level_binary.hpp"

#include <cstring>
#include <fstream>
#include <sstream>

namespace engine {

namespace {

void write_u32(std::vector<uint8_t>& b, uint32_t v)
{
	b.push_back(static_cast<uint8_t>(v & 0xff));
	b.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
	b.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
	b.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}

void write_i32(std::vector<uint8_t>& b, int32_t v)
{
	write_u32(b, static_cast<uint32_t>(v));
}

void write_f32(std::vector<uint8_t>& b, float f)
{
	uint32_t u = 0;
	std::memcpy(&u, &f, sizeof(u));
	write_u32(b, u);
}

void write_bytes(std::vector<uint8_t>& b, const void* src, size_t n)
{
	const uint8_t* s = static_cast<const uint8_t*>(src);
	b.insert(b.end(), s, s + n);
}

void write_string(std::vector<uint8_t>& b, const std::string& s)
{
	write_u32(b, static_cast<uint32_t>(s.size()));
	if (!s.empty()) {
		write_bytes(b, s.data(), s.size());
	}
}

struct Reader {
	const uint8_t* p;
	const uint8_t* end;
	bool failed = false;

	Reader(const uint8_t* d, size_t n) : p(d), end(d + n) {}

	bool read_u32(uint32_t& out)
	{
		if (failed || p + 4 > end) { failed = true; return false; }
		out = static_cast<uint32_t>(p[0])
			| (static_cast<uint32_t>(p[1]) << 8)
			| (static_cast<uint32_t>(p[2]) << 16)
			| (static_cast<uint32_t>(p[3]) << 24);
		p += 4;
		return true;
	}
	bool read_i32(int32_t& out)
	{
		uint32_t u = 0;
		if (!read_u32(u)) return false;
		out = static_cast<int32_t>(u);
		return true;
	}
	bool read_f32(float& out)
	{
		uint32_t u = 0;
		if (!read_u32(u)) return false;
		std::memcpy(&out, &u, sizeof(out));
		return true;
	}
	bool read_bytes(void* dst, size_t n)
	{
		if (failed || p + n > end) { failed = true; return false; }
		std::memcpy(dst, p, n);
		p += n;
		return true;
	}
	bool read_u8(uint8_t& out)
	{
		if (failed || p + 1 > end) { failed = true; return false; }
		out = *p++;
		return true;
	}
	bool read_string(std::string& out)
	{
		uint32_t n = 0;
		if (!read_u32(n)) return false;
		out.resize(n);
		if (n > 0 && !read_bytes(out.data(), n)) return false;
		return true;
	}
};

} // namespace

void write_level_binary(const Level& level, std::vector<uint8_t>& out_bytes)
{
	out_bytes.clear();
	write_bytes(out_bytes, k_evil_magic, 4);
	write_u32(out_bytes, k_evil_version);

	write_string(out_bytes, level.name);
	write_f32(out_bytes, level.wall_height);
	write_f32(out_bytes, level.ambient[0]);
	write_f32(out_bytes, level.ambient[1]);
	write_f32(out_bytes, level.ambient[2]);

	write_f32(out_bytes, level.spawn.pos.x);
	write_f32(out_bytes, level.spawn.pos.y);
	write_f32(out_bytes, level.spawn.pos.z);
	write_f32(out_bytes, level.spawn.yaw_deg);

	write_u32(out_bytes, static_cast<uint32_t>(level.sectors.size()));
	for (const Sector& s : level.sectors) {
		write_string(out_bytes, s.id);
		write_u32(out_bytes, static_cast<uint32_t>(s.polygon.size()));
		for (const Vec2& p : s.polygon) {
			write_f32(out_bytes, p.x);
			write_f32(out_bytes, p.z);
		}
		write_f32(out_bytes, s.floor_y);
		write_f32(out_bytes, s.ceiling_y);
	}

	write_u32(out_bytes, static_cast<uint32_t>(level.walls.size()));
	for (const Wall& w : level.walls) {
		out_bytes.push_back(static_cast<uint8_t>(w.type));
		write_f32(out_bytes, w.a.x);
		write_f32(out_bytes, w.a.z);
		write_f32(out_bytes, w.b.x);
		write_f32(out_bytes, w.b.z);
		write_f32(out_bytes, w.y0);
		write_f32(out_bytes, w.y1);
		write_f32(out_bytes, w.thickness);
		write_f32(out_bytes, w.door_width);
		write_f32(out_bytes, w.door_offset);
		write_f32(out_bytes, w.door_height);
	}

	write_u32(out_bytes, static_cast<uint32_t>(level.stairs.size()));
	for (const Stair& s : level.stairs) {
		write_f32(out_bytes, s.center_a.x);
		write_f32(out_bytes, s.center_a.z);
		write_f32(out_bytes, s.center_b.x);
		write_f32(out_bytes, s.center_b.z);
		write_f32(out_bytes, s.width);
		write_f32(out_bytes, s.from_y);
		write_f32(out_bytes, s.to_y);
		write_i32(out_bytes, s.steps);
	}

	write_u32(out_bytes, static_cast<uint32_t>(level.lights.size()));
	for (const Light& l : level.lights) {
		write_f32(out_bytes, l.pos.x);
		write_f32(out_bytes, l.pos.y);
		write_f32(out_bytes, l.pos.z);
		write_f32(out_bytes, l.color[0]);
		write_f32(out_bytes, l.color[1]);
		write_f32(out_bytes, l.color[2]);
		write_f32(out_bytes, l.intensity);
	}
}

bool parse_level_binary(const uint8_t* data, size_t size, Level& out, std::string& err)
{
	out = Level{};
	Reader r(data, size);

	char magic[4];
	if (!r.read_bytes(magic, 4) || std::memcmp(magic, k_evil_magic, 4) != 0) {
		err = "bad magic (not a .evil v2 level)";
		return false;
	}
	uint32_t version = 0;
	if (!r.read_u32(version) || version != k_evil_version) {
		err = "unsupported .evil version";
		return false;
	}
	out.version = static_cast<int>(version);

	if (!r.read_string(out.name)) {
		err = "truncated name";
		return false;
	}
	if (!r.read_f32(out.wall_height)) {
		err = "truncated wall_height";
		return false;
	}
	for (int i = 0; i < 3; ++i) {
		if (!r.read_f32(out.ambient[static_cast<size_t>(i)])) {
			err = "truncated ambient";
			return false;
		}
	}

	if (!r.read_f32(out.spawn.pos.x) || !r.read_f32(out.spawn.pos.y) || !r.read_f32(out.spawn.pos.z)
		|| !r.read_f32(out.spawn.yaw_deg)) {
		err = "truncated spawn";
		return false;
	}

	uint32_t ns = 0;
	if (!r.read_u32(ns)) {
		err = "truncated sectors count";
		return false;
	}
	out.sectors.resize(ns);
	for (uint32_t i = 0; i < ns; ++i) {
		Sector& s = out.sectors[i];
		if (!r.read_string(s.id)) {
			err = "truncated sector id";
			return false;
		}
		uint32_t np = 0;
		if (!r.read_u32(np)) {
			err = "truncated sector poly count";
			return false;
		}
		s.polygon.resize(np);
		for (uint32_t j = 0; j < np; ++j) {
			if (!r.read_f32(s.polygon[j].x) || !r.read_f32(s.polygon[j].z)) {
				err = "truncated sector point";
				return false;
			}
		}
		if (!r.read_f32(s.floor_y) || !r.read_f32(s.ceiling_y)) {
			err = "truncated sector y";
			return false;
		}
	}

	uint32_t nw = 0;
	if (!r.read_u32(nw)) {
		err = "truncated walls count";
		return false;
	}
	out.walls.resize(nw);
	for (uint32_t i = 0; i < nw; ++i) {
		Wall& w = out.walls[i];
		uint8_t t = 0;
		if (!r.read_u8(t)) { err = "truncated wall"; return false; }
		w.type = static_cast<WallType>(t);
		if (!r.read_f32(w.a.x) || !r.read_f32(w.a.z)
			|| !r.read_f32(w.b.x) || !r.read_f32(w.b.z)
			|| !r.read_f32(w.y0) || !r.read_f32(w.y1)
			|| !r.read_f32(w.thickness)
			|| !r.read_f32(w.door_width) || !r.read_f32(w.door_offset) || !r.read_f32(w.door_height)) {
			err = "truncated wall";
			return false;
		}
	}

	uint32_t nst = 0;
	if (!r.read_u32(nst)) { err = "truncated stairs count"; return false; }
	out.stairs.resize(nst);
	for (uint32_t i = 0; i < nst; ++i) {
		Stair& s = out.stairs[i];
		if (!r.read_f32(s.center_a.x) || !r.read_f32(s.center_a.z)
			|| !r.read_f32(s.center_b.x) || !r.read_f32(s.center_b.z)
			|| !r.read_f32(s.width) || !r.read_f32(s.from_y) || !r.read_f32(s.to_y)
			|| !r.read_i32(s.steps)) {
			err = "truncated stair";
			return false;
		}
	}

	uint32_t nl = 0;
	if (!r.read_u32(nl)) { err = "truncated lights count"; return false; }
	out.lights.resize(nl);
	for (uint32_t i = 0; i < nl; ++i) {
		Light& l = out.lights[i];
		if (!r.read_f32(l.pos.x) || !r.read_f32(l.pos.y) || !r.read_f32(l.pos.z)
			|| !r.read_f32(l.color[0]) || !r.read_f32(l.color[1]) || !r.read_f32(l.color[2])
			|| !r.read_f32(l.intensity)) {
			err = "truncated light";
			return false;
		}
	}

	return true;
}

bool load_level_binary(const char* path, Level& out, std::string& err)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) {
		err = std::string("cannot open: ") + path;
		return false;
	}
	std::ostringstream ss;
	ss << f.rdbuf();
	const std::string s = ss.str();
	return parse_level_binary(reinterpret_cast<const uint8_t*>(s.data()), s.size(), out, err);
}

bool save_level_binary(const char* path, const Level& level, std::string& err)
{
	std::vector<uint8_t> bytes;
	write_level_binary(level, bytes);
	std::ofstream f(path, std::ios::binary);
	if (!f) {
		err = std::string("cannot write: ") + path;
		return false;
	}
	f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	return f.good();
}

} // namespace engine
