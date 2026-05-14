#pragma once

#include <bgfx/bgfx.h>

namespace engine {

/// Loads an image (JPEG/PNG/etc. via bimg_decode) into a 2D texture. Returns
/// invalid handle on failure. Memory is owned by the texture (release callback).
bgfx::TextureHandle load_texture_from_file(const char* path, uint64_t flags = 0);

/// Loads a texture from raw file bytes already in memory (same format support as above).
bgfx::TextureHandle load_texture_from_memory(const void* data, size_t size,
                                              const char* debug_name = nullptr,
                                              uint64_t flags = 0);

} // namespace engine
