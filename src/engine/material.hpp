#pragma once

#include <bgfx/bgfx.h>

namespace engine {

struct Material {
    bgfx::TextureHandle albedo    = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle normal    = BGFX_INVALID_HANDLE; // tangent-space NormalGL map
    bgfx::TextureHandle roughness = BGFX_INVALID_HANDLE; // grayscale: 0=smooth, 1=rough
};

} // namespace engine
