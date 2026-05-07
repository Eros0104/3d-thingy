#pragma once

#include "game/level/level_data.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

/// Compact binary representation of a v4 `Level`. All fields little-endian.
///
///   magic       : char[4]  = "EVIL"
///   version     : uint32   = 4
///   name_len    : uint32 + bytes
///   wall_height : float32
///   ambient     : float32[3]
///   spawn       : float32[3] pos, float32 yaw_deg
///   sectors_n   : uint32
///   for each sector:
///     id_len : uint32 + bytes
///     poly_n : uint32
///     poly   : (float32, float32)[poly_n]   // x, z pairs
///     floor_y, ceiling_y : float32 x 2
///   walls_n : uint32
///   for each wall:
///     a.x, a.y, a.z, b.x, b.y, b.z : float32 x 6
///     height : float32
///   brushes_n : uint32
///   for each brush:
///     wall   : int32
///     offset, width, y_start, height : float32 x 4
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
constexpr uint32_t k_evil_version = 4;

void write_level_binary(const Level& level, std::vector<uint8_t>& out_bytes);
bool parse_level_binary(const uint8_t* data, size_t size, Level& out, std::string& err);
bool load_level_binary(const char* path, Level& out, std::string& err);
bool save_level_binary(const char* path, const Level& level, std::string& err);

} // namespace engine
