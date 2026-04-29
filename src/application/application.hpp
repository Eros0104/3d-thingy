#pragma once

#include <SDL.h>

namespace application {

// Core lifecycle. Call init() exactly once at startup and cleanup() exactly
// once at shutdown. init() owns the SDL subsystems, the main window, and the
// bgfx renderer; on failure it tears down whatever it had partially brought
// up so the caller can simply bail out.
bool init(const char *window_title, int window_width, int window_height);
void cleanup();

// Main-loop control. is_running() returns true while the window is alive and
// quit() hasn't been called; quit() flips that flag (call it from your
// SDL_QUIT handler) so the next iteration of the loop exits.
bool is_running();
void quit();

// Window
SDL_Window *get_window();
int get_current_window_width();
int get_current_window_height();

// Call when an SDL_WINDOWEVENT_RESIZED / SIZE_CHANGED event arrives. Re-reads
// the drawable size in pixels and updates bgfx's swap chain + view rects
// (views 0 and 1) accordingly.
void on_window_resized();

} // namespace application
