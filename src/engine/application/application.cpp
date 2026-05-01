#include "engine/application/application.hpp"

#include <SDL.h>

#include <cstdio>

bool Application::init() {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return false;
  }
  m_initialized = true;
  return true;
}

Application::~Application() {
  if (m_initialized)
    SDL_Quit();
}
