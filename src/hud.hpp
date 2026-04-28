#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>
#include <string>

namespace engine {

struct HudInitDesc {
	const char* font_path = nullptr;
	float pixel_height = 32.0f;
	bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
};

bool hud_init(const HudInitDesc& desc, std::string& err);
void hud_shutdown();

// Lays out 2D quads in pixel-space. The view used must already have the
// orthographic projection set by hud_begin_frame().
void hud_begin_frame(bgfx::ViewId view, int screen_w, int screen_h);

// Submits text at (pen_x, pen_y) — pen_y is the baseline. Color is 0xAABBGGRR.
// Returns the advance width in pixels.
float hud_draw_text(const char* text, float pen_x, float pen_y, uint32_t abgr);

// Same as above but draws so the right edge of the text aligns to right_x.
float hud_draw_text_right(const char* text, float right_x, float pen_y, uint32_t abgr);

float hud_text_width(const char* text);
float hud_line_height();
float hud_ascent();
float hud_descent();

} // namespace engine
