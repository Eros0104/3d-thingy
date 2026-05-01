#include "engine/window.hpp"

#include <SDL_syswm.h>

bool Window::create(const char *title, int w, int h) {
  window =
      SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w,
                       h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  return window != nullptr;
}

bool Window::is_running() const { return window != nullptr; }

void Window::quit() { destroy(); }

SDL_Window *Window::get() { return window; }

void Window::destroy() {
  if (window == nullptr)
    return;
  SDL_DestroyWindow(window);
  window = nullptr;
}

void Window::pixel_size(int *out_w, int *out_h) const {
#if SDL_VERSION_ATLEAST(2, 26, 0)
  SDL_GetWindowSizeInPixels(window, out_w, out_h);
#else
  SDL_GetWindowSize(window, out_w, out_h);
#endif
}

void *Window::native_window_handle() const {
  SDL_SysWMinfo wmi;
  SDL_VERSION(&wmi.version);
  if (!SDL_GetWindowWMInfo(window, &wmi))
    return nullptr;

#if defined(SDL_VIDEO_DRIVER_WINDOWS)
  return reinterpret_cast<void *>(wmi.info.win.window);
#elif defined(SDL_VIDEO_DRIVER_COCOA)
  return wmi.info.cocoa.window;
#elif defined(SDL_VIDEO_DRIVER_X11)
  return reinterpret_cast<void *>(static_cast<uintptr_t>(wmi.info.x11.window));
#elif defined(SDL_VIDEO_DRIVER_WAYLAND)
  return wmi.info.wl.surface;
#else
  return nullptr;
#endif
}

void *Window::native_display_handle() const {
  SDL_SysWMinfo wmi;
  SDL_VERSION(&wmi.version);
  if (!SDL_GetWindowWMInfo(window, &wmi))
    return nullptr;

#if defined(SDL_VIDEO_DRIVER_X11)
  return wmi.info.x11.display;
#else
  return nullptr;
#endif
}

Window::~Window() { destroy(); }
