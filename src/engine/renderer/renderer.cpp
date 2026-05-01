#include "engine/renderer/renderer.hpp"

#include "engine/window.hpp"

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#include <cstdio>

bool Renderer::init(Window &window) {
  window.pixel_size(&m_width, &m_height);

  bgfx::PlatformData pd{};
  pd.nwh = window.native_window_handle();
  pd.ndt = window.native_display_handle();
  if (!pd.nwh) {
    std::fprintf(stderr,
                 "Native window handle is null (SDL video driver unsupported?)\n");
    return false;
  }

  bgfx::Init bgfx_init;
  bgfx_init.platformData = pd;
  bgfx_init.resolution.width = static_cast<uint32_t>(m_width);
  bgfx_init.resolution.height = static_cast<uint32_t>(m_height);
  bgfx_init.resolution.reset = BGFX_RESET_VSYNC;
  if (!bgfx::init(bgfx_init)) {
    std::fprintf(stderr, "bgfx::init failed\n");
    return false;
  }

  bgfx::setDebug(BGFX_DEBUG_TEXT);
  bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x1a1a2eff,
                     1.0f, 0);
  bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(m_width),
                    static_cast<uint16_t>(m_height));
  bgfx::setViewClear(1, BGFX_CLEAR_DEPTH, 0x00000000, 1.0f, 0);
  bgfx::setViewRect(1, 0, 0, static_cast<uint16_t>(m_width),
                    static_cast<uint16_t>(m_height));

  m_initialized = true;
  return true;
}

Renderer::~Renderer() {
  if (m_initialized)
    bgfx::shutdown();
}

void Renderer::resize(int w, int h) {
  m_width = w;
  m_height = h;
  bgfx::reset(static_cast<uint32_t>(w), static_cast<uint32_t>(h),
              BGFX_RESET_VSYNC);
  bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(w),
                    static_cast<uint16_t>(h));
  bgfx::setViewRect(1, 0, 0, static_cast<uint16_t>(w),
                    static_cast<uint16_t>(h));
}

void Renderer::begin_frame() { bgfx::touch(0); }
void Renderer::end_frame() { bgfx::frame(); }
int Renderer::width() const { return m_width; }
int Renderer::height() const { return m_height; }
