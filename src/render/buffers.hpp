#pragma once

#include "lit_vertex.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <vector>

namespace engine {

// Copies a span of LitVertex into bgfx-owned memory and returns a vertex
// buffer handle for it. Empty inputs return BGFX_INVALID_HANDLE so callers
// can handle missing geometry without an extra branch at the call site.
inline bgfx::VertexBufferHandle
create_vertex_buffer(const std::vector<LitVertex> &verts,
                     const bgfx::VertexLayout &layout) {
  if (verts.empty()) {
    return BGFX_INVALID_HANDLE;
  }
  const bgfx::Memory *mem = bgfx::copy(
      verts.data(), static_cast<uint32_t>(verts.size() * sizeof(LitVertex)));
  return bgfx::createVertexBuffer(mem, layout);
}

} // namespace engine
