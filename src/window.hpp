#pragma once

#include <SDL.h>
#include <SDL_syswm.h>

class Window {

private:
  SDL_Window *window = nullptr;

public:
  bool create(const char *title, int w, int h);
  bool is_running();
  void quit();
  SDL_Window *get();
  void destroy();
  ~Window();
};
