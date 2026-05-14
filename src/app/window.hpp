#pragma once

#include <SDL.h>

class Window {

private:
  SDL_Window *window = nullptr;

public:
  bool create(const char *title, int w, int h);
  bool is_running() const;
  void quit();
  SDL_Window *get();
  void destroy();
  void pixel_size(int *out_w, int *out_h) const;
  void *native_window_handle() const;
  void *native_display_handle() const;
  ~Window();
};
