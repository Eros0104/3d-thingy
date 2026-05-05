#include "engine/application/application.hpp"
#include "engine/renderer/renderer.hpp"
#include "engine/window.hpp"
#include "game/game_state.hpp"

#include <SDL.h>

#include <cstdio>

#ifndef ENGINE_MAPS_DIR
#define ENGINE_MAPS_DIR "maps"
#endif

int main(int argc, char** argv) {
    const char* level_path = ENGINE_MAPS_DIR "/mansion.json";
    if (argc >= 2) level_path = argv[1];

    Application app;
    if (!app.init()) return 1;

    Window window;
    if (!window.create("fps-engine (SDL2 + bgfx)", 1280, 720)) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }

    Renderer renderer;
    if (!renderer.init(window)) return 1;

    int width  = renderer.width();
    int height = renderer.height();

    GameState state;
    if (!state.init(level_path, width, height)) return 1;

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
        if (dt > 0.1f) dt = 0.1f;

        state.update(dt);
        renderer.begin_frame();
        state.render(width, height);
        renderer.end_frame();
    }

    state.shutdown();
    return 0;
}
