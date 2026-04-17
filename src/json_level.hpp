#pragma once

#include "level_data.hpp"

#include <string>

namespace engine {

/// Parse a v2 .json level file at `path` into `out`. Returns false and fills `err` on any
/// validation failure. Schema (informal):
///
/// {
///   "version": 2,
///   "name": "<level name>",
///   "wall_height": 3.2,                  // default for walls (y0=0, y1=wall_height)
///   "ambient": [r, g, b],
///   "spawn": { "pos": [x, y, z], "yaw_deg": 0 },
///   "sectors": [
///     {
///       "id": "main_hall",               // optional
///       "polygon": [[x, z], [x, z], ...],
///       "floor_y": 0.0,
///       "ceiling_y": 3.2
///     }
///   ],
///   "walls": [
///     { "type": "normal", "a": [x, z], "b": [x, z] },
///     { "type": "door", "a": [x, z], "b": [x, z],
///       "door_width": 1.2, "door_offset": null, "door_height": 2.2 },
///     { "type": "broken", "a": [x, z], "b": [x, z] },
///     { "type": "window", "a": [x, z], "b": [x, z] }
///   ],
///   "stairs": [
///     { "center_a": [x, z], "center_b": [x, z],
///       "width": 2.0, "from_y": 0.0, "to_y": 3.2, "steps": 8 }
///   ],
///   "lights": [
///     { "pos": [x, y, z], "color": [r, g, b], "intensity": 1.0 }
///   ]
/// }
bool load_json_level(const char* path, Level& out, std::string& err);

/// Parse JSON text (same schema) into `out`. Useful for tools and tests.
bool parse_json_level(const std::string& text, Level& out, std::string& err);

} // namespace engine
