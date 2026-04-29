#include "window.hpp"

bool Window::create(const char *title, int w, int h) {
  window =
      SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w,
                       h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

  if (!window) {
    return false;
  }

  return true;
}

bool Window::is_running() { return window != nullptr; }

void Window::quit() { destroy(); }

SDL_Window *Window::get() { return window; }

void Window::destroy() {
  if (window == nullptr)
    return;

  SDL_DestroyWindow(window);
  window = nullptr;
}

Window::~Window() { destroy(); }
