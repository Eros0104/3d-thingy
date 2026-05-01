#include "game/level/json_level.hpp"
#include "game/level/level_binary.hpp"

#include <cstdio>
#include <string>

int main(int argc, char** argv)
{
	if (argc < 3) {
		std::fprintf(stderr,
			"usage: level_pack <input.json> <output.evil>\n"
			"  Converts a JSON level into the runtime .evil binary format.\n");
		return 2;
	}

	const char* in_path = argv[1];
	const char* out_path = argv[2];

	engine::Level level;
	std::string err;
	if (!engine::load_json_level(in_path, level, err)) {
		std::fprintf(stderr, "level_pack: load %s failed: %s\n", in_path, err.c_str());
		return 1;
	}
	if (!engine::save_level_binary(out_path, level, err)) {
		std::fprintf(stderr, "level_pack: save %s failed: %s\n", out_path, err.c_str());
		return 1;
	}

	std::printf(
		"level_pack: %s -> %s (sectors=%zu, walls=%zu, stairs=%zu, lights=%zu)\n",
		in_path, out_path,
		level.sectors.size(), level.walls.size(), level.stairs.size(), level.lights.size()
	);
	return 0;
}
