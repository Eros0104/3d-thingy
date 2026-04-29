#include "application.hpp"

#include "window.hpp"

#include <SDL_syswm.h>

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#include <cstdio>

namespace application {

namespace {

Window g_window;
int g_width = 0;
int g_height = 0;
bool g_bgfx_initialized = false;

void *native_window_handle(SDL_Window *window) {
  SDL_SysWMinfo wmi;
  SDL_VERSION(&wmi.version);
  if (!SDL_GetWindowWMInfo(window, &wmi)) {
    return nullptr;
  }

#if defined(SDL_VIDEO_DRIVER_WINDOWS)
  return reinterpret_cast<void *>(wmi.info.win.window);
#elif defined(SDL_VIDEO_DRIVER_COCOA)
  return wmi.info.cocoa.window;
#elif defined(SDL_VIDEO_DRIVER_X11)
  return reinterpret_cast<void *>(static_cast<uintptr_t>(wmi.info.x11.window));
#elif defined(SDL_VIDEO_DRIVER_WAYLAND)
  return wmi.info.wl.surface;
#else
  (void)window;
  return nullptr;
#endif
}

void *native_display_handle(SDL_Window *window) {
  SDL_SysWMinfo wmi;
  SDL_VERSION(&wmi.version);
  if (!SDL_GetWindowWMInfo(window, &wmi)) {
    return nullptr;
  }

#if defined(SDL_VIDEO_DRIVER_X11)
  return wmi.info.x11.display;
#else
  (void)window;
  return nullptr;
#endif
}

void window_pixel_size(SDL_Window *window, int *out_w, int *out_h) {
#if SDL_VERSION_ATLEAST(2, 26, 0)
  SDL_GetWindowSizeInPixels(window, out_w, out_h);
#else
  SDL_GetWindowSize(window, out_w, out_h);
#endif
}

} // namespace

bool init(const char *window_title, int window_width, int window_height) {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return false;
  }

  if (!g_window.create(window_title, window_width, window_height)) {
    std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    SDL_Quit();
    return false;
  }

  bgfx::PlatformData pd{};
  pd.nwh = native_window_handle(g_window.get());
  pd.ndt = native_display_handle(g_window.get());
  if (!pd.nwh) {
    std::fprintf(
        stderr,
        "Native window handle is null (SDL video driver unsupported?)\n");
    g_window.destroy();
    SDL_Quit();
    return false;
  }

  window_pixel_size(g_window.get(), &g_width, &g_height);

  bgfx::Init bgfx_init;
  bgfx_init.platformData = pd;
  bgfx_init.resolution.width = static_cast<uint32_t>(g_width);
  bgfx_init.resolution.height = static_cast<uint32_t>(g_height);
  bgfx_init.resolution.reset = BGFX_RESET_VSYNC;
  if (!bgfx::init(bgfx_init)) {
    std::fprintf(stderr, "bgfx::init failed\n");
    g_window.destroy();
    SDL_Quit();
    return false;
  }
  g_bgfx_initialized = true;

  bgfx::setDebug(BGFX_DEBUG_TEXT);
  bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x1a1a2eff, 1.0f,
                     0);
  bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(g_width),
                    static_cast<uint16_t>(g_height));

  // View 1 is the viewmodel pass: it shares the camera projection but clears
  // depth so the weapon never z-fights or pokes through walls.
  bgfx::setViewClear(1, BGFX_CLEAR_DEPTH, 0x00000000, 1.0f, 0);
  bgfx::setViewRect(1, 0, 0, static_cast<uint16_t>(g_width),
                    static_cast<uint16_t>(g_height));

  return true;
}

void cleanup() {
  if (g_bgfx_initialized) {
    bgfx::shutdown();
    g_bgfx_initialized = false;
  }
  g_window.destroy();
  SDL_Quit();
}

bool is_running() { return g_window.is_running(); }
void quit() { g_window.quit(); }

SDL_Window *get_window() { return g_window.get(); }
int get_current_window_width() { return g_width; }
int get_current_window_height() { return g_height; }

void on_window_resized() {
  window_pixel_size(g_window.get(), &g_width, &g_height);
  bgfx::reset(static_cast<uint32_t>(g_width), static_cast<uint32_t>(g_height),
              BGFX_RESET_VSYNC);
  bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(g_width),
                    static_cast<uint16_t>(g_height));
  bgfx::setViewRect(1, 0, 0, static_cast<uint16_t>(g_width),
                    static_cast<uint16_t>(g_height));
}

} // namespace application
