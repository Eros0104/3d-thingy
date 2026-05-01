#pragma once

#include "game/level/level_data.hpp"

#include <string>

namespace engine {

// Loads a level from disk. Files ending in ".evil" are read as the runtime
// binary format; everything else is parsed as JSON. On failure returns false
// and writes a human-readable message into `err`.
bool load_level_any(const char *path, Level &out, std::string &err);

} // namespace engine
