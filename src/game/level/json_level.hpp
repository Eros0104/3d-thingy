#pragma once

#include "game/level/level_data.hpp"

#include <string>

namespace engine {

/// Parse a v3 .json level file at `path` into `out`. Returns false and fills `err` on any
/// validation failure. Schema (informal):
///
/// {
///   "version": 3,
///   "name": "<level name>",
///   "wall_height": 3.2,          // default height for walls that omit the field
///   "ambient": [r, g, b],
///   "spawn": { "pos": [x, y, z], "yaw_deg": 0 },
///   "sectors": [
///     { "id": "main_hall", "polygon": [[x, z], ...], "floor_y": 0, "ceiling_y": 3.2 }
///   ],
///   "walls": [
///     { "a": [x, y, z], "b": [x, y, z], "height": 3.2 }
///   ],
///   "brushes": [
///     { "wall": 0, "offset": 1.9, "width": 1.2, "y_start": 0.0, "height": 2.2 }
///   ],
///   "stairs": [
///     { "center_a": [x, z], "center_b": [x, z], "width": 2.0, "from_y": 0, "to_y": 3.2, "steps": 8 }
///   ],
///   "lights": [
///     { "pos": [x, y, z], "color": [r, g, b], "intensity": 1.0 }
///   ]
/// }
bool load_json_level(const char* path, Level& out, std::string& err);

/// Parse JSON text (same schema) into `out`. Useful for tools and tests.
bool parse_json_level(const std::string& text, Level& out, std::string& err);

} // namespace engine
