#pragma once

#include "level_data.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

/// Compact binary representation of a v2 `Level`. All fields little-endian.
///
///   magic       : char[4]  = "EVIL"
///   version     : uint32   = 2
///   name_len    : uint32 + bytes
///   wall_height : float32
///   ambient     : float32[3]
///   spawn       : float32[3] pos, float32 yaw_deg
///   sectors_n   : uint32
///   for each sector:
///     id_len : uint32 + bytes
///     poly_n : uint32
///     poly   : (float32, float32)[poly_n]
///     floor_y, ceiling_y : float32, float32
///   walls_n : uint32
///   for each wall:
///     type : uint8 (WallType)
///     a.x, a.z, b.x, b.z : float32 x 4
///     y0, y1 : float32 x 2
///     door_width, door_offset, door_height : float32 x 3
///   stairs_n : uint32
///   for each stair:
///     center_a.x, center_a.z, center_b.x, center_b.z : float32 x 4
///     width, from_y, to_y : float32 x 3
///     steps : int32
///   lights_n : uint32
///   for each light:
///     pos : float32[3]
///     color : float32[3]
///     intensity : float32
constexpr char k_evil_magic[4] = {'E', 'V', 'I', 'L'};
constexpr uint32_t k_evil_version = 2;

/// Serialize `level` into a byte vector.
void write_level_binary(const Level& level, std::vector<uint8_t>& out_bytes);

/// Parse binary bytes into `out`. Returns false and fills `err` on failure.
bool parse_level_binary(const uint8_t* data, size_t size, Level& out, std::string& err);

/// Read a .evil binary file from disk into `out`.
bool load_level_binary(const char* path, Level& out, std::string& err);

/// Write `level` to `path` as a .evil binary file.
bool save_level_binary(const char* path, const Level& level, std::string& err);

} // namespace engine
