#include "app/application.hpp"
#include "app/log.hpp"
#include "render/renderer.hpp"
#include "app/window.hpp"
#include "game_state.hpp"

#include <SDL.h>

#ifndef ENGINE_MAPS_DIR
#define ENGINE_MAPS_DIR "maps"
#endif

int main(int argc, char **argv) {
  engine::log_init(engine::LogLevel::Debug);

  // const char *level_path = ENGINE_MAPS_DIR "/spencer_mansion_v3.evil";
  const char *level_path = ENGINE_MAPS_DIR "/untitled.evil";
  if (argc >= 2)
    level_path = argv[1];

  Application app;
  if (!app.init())
    return 1;

  Window window;
  if (!window.create("fps-engine (SDL2 + bgfx)", 1280, 720)) {
    LOG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
    return 1;
  }

  Renderer renderer;
  if (!renderer.init(window))
    return 1;

  int width = renderer.width();
  int height = renderer.height();

  GameState state;
  if (!state.init(level_path, width, height))
    return 1;

  uint64_t prev_ticks = SDL_GetTicks64();
  while (window.is_running()) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        window.quit();
      } else if (event.type == SDL_WINDOWEVENT &&
                 (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                  event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
        window.pixel_size(&width, &height);
        renderer.resize(width, height);
        state.on_resize(width, height);
      }
      state.handle_event(event);
    }

    const uint64_t now = SDL_GetTicks64();
    float dt = static_cast<float>(now - prev_ticks) * 0.001f;
    prev_ticks = now;
    if (dt > 0.1f)
      dt = 0.1f;

    state.update(dt);
    renderer.begin_frame();
    state.render(width, height);
    renderer.end_frame();
  }

  state.shutdown();
  engine::log_shutdown();
  return 0;
}
