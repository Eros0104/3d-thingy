#include "hud.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <bx/math.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace engine {

namespace {

constexpr int k_first_char = 32;
constexpr int k_char_count = 96; // 32..127
constexpr int k_atlas_w = 512;
constexpr int k_atlas_h = 512;

struct HudVertex {
	float x;
	float y;
	float z;
	float u;
	float v;
	uint32_t abgr;
};

bgfx::VertexLayout g_layout;
bool g_layout_inited = false;

bgfx::TextureHandle g_atlas_tex = BGFX_INVALID_HANDLE;
bgfx::UniformHandle g_s_albedo = BGFX_INVALID_HANDLE;
bgfx::ProgramHandle g_program = BGFX_INVALID_HANDLE;
std::vector<stbtt_bakedchar> g_chars;
float g_pixel_height = 32.0f;
float g_ascent = 0.0f;
float g_descent = 0.0f;
float g_line_gap = 0.0f;
bool g_initialized = false;

bgfx::ViewId g_view = 0;
int g_screen_w = 0;
int g_screen_h = 0;

bool read_file_bytes(const char* path, std::vector<uint8_t>& out)
{
	std::ifstream f(path, std::ios::binary | std::ios::ate);
	if (!f) {
		return false;
	}
	const std::streamsize sz = f.tellg();
	if (sz <= 0) {
		return false;
	}
	f.seekg(0, std::ios::beg);
	out.resize(static_cast<size_t>(sz));
	return static_cast<bool>(f.read(reinterpret_cast<char*>(out.data()), sz));
}

const stbtt_bakedchar* glyph(int codepoint)
{
	if (codepoint < k_first_char || codepoint >= k_first_char + k_char_count) {
		return nullptr;
	}
	return &g_chars[static_cast<size_t>(codepoint - k_first_char)];
}

} // namespace

bool hud_init(const HudInitDesc& desc, std::string& err)
{
	hud_shutdown();

	if (!desc.font_path) {
		err = "hud: font_path is null";
		return false;
	}
	if (!bgfx::isValid(desc.program)) {
		err = "hud: invalid program";
		return false;
	}

	std::vector<uint8_t> font_data;
	if (!read_file_bytes(desc.font_path, font_data)) {
		err = std::string("hud: failed to read ") + desc.font_path;
		return false;
	}

	g_pixel_height = desc.pixel_height;

	std::vector<uint8_t> atlas(static_cast<size_t>(k_atlas_w) * k_atlas_h, 0);
	g_chars.assign(k_char_count, stbtt_bakedchar{});

	const int baked = stbtt_BakeFontBitmap(
		font_data.data(),
		0,
		g_pixel_height,
		atlas.data(),
		k_atlas_w,
		k_atlas_h,
		k_first_char,
		k_char_count,
		g_chars.data()
	);
	if (baked <= 0) {
		err = "hud: stbtt_BakeFontBitmap failed (atlas too small for size?)";
		return false;
	}

	// Reserve the bottom-right texel as a solid 1x1 white pixel so we can draw
	// untextured filled rects through the same alpha-tinting fragment shader.
	atlas[static_cast<size_t>(k_atlas_h - 1) * k_atlas_w + (k_atlas_w - 1)] = 255;

	stbtt_fontinfo info;
	if (!stbtt_InitFont(&info, font_data.data(), 0)) {
		err = "hud: stbtt_InitFont failed";
		return false;
	}
	int ascent = 0;
	int descent = 0;
	int line_gap = 0;
	stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
	const float scale = stbtt_ScaleForPixelHeight(&info, g_pixel_height);
	g_ascent = ascent * scale;
	g_descent = descent * scale;
	g_line_gap = line_gap * scale;

	if (!g_layout_inited) {
		g_layout.begin()
			.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
			.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
			.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
			.end();
		g_layout_inited = true;
	}

	const bgfx::Memory* mem = bgfx::copy(atlas.data(), static_cast<uint32_t>(atlas.size()));
	g_atlas_tex = bgfx::createTexture2D(
		k_atlas_w,
		k_atlas_h,
		false,
		1,
		bgfx::TextureFormat::R8,
		0,
		mem
	);
	if (!bgfx::isValid(g_atlas_tex)) {
		err = "hud: failed to create atlas texture";
		return false;
	}
	bgfx::setName(g_atlas_tex, "hud_font_atlas");

	g_s_albedo = bgfx::createUniform("s_albedo", bgfx::UniformType::Sampler);
	g_program = desc.program;
	g_initialized = true;
	return true;
}

void hud_shutdown()
{
	if (bgfx::isValid(g_atlas_tex)) {
		bgfx::destroy(g_atlas_tex);
		g_atlas_tex = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(g_s_albedo)) {
		bgfx::destroy(g_s_albedo);
		g_s_albedo = BGFX_INVALID_HANDLE;
	}
	g_program = BGFX_INVALID_HANDLE;
	g_chars.clear();
	g_initialized = false;
}

void hud_begin_frame(bgfx::ViewId view, int screen_w, int screen_h)
{
	g_view = view;
	g_screen_w = screen_w;
	g_screen_h = screen_h;

	float ortho[16];
	const bgfx::Caps* caps = bgfx::getCaps();
	bx::mtxOrtho(
		ortho,
		0.0f,
		static_cast<float>(screen_w),
		static_cast<float>(screen_h),
		0.0f,
		-1.0f,
		1.0f,
		0.0f,
		caps->homogeneousDepth
	);
	bgfx::setViewRect(view, 0, 0, static_cast<uint16_t>(screen_w), static_cast<uint16_t>(screen_h));
	bgfx::setViewTransform(view, nullptr, ortho);
	bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
	bgfx::setViewClear(view, BGFX_CLEAR_NONE);
	bgfx::touch(view);
}

float hud_text_width(const char* text)
{
	if (!g_initialized || !text) {
		return 0.0f;
	}
	float x = 0.0f;
	float y = 0.0f;
	for (const char* p = text; *p; ++p) {
		const stbtt_bakedchar* c = glyph(static_cast<unsigned char>(*p));
		if (!c) {
			continue;
		}
		stbtt_aligned_quad q;
		stbtt_GetBakedQuad(
			g_chars.data(),
			k_atlas_w,
			k_atlas_h,
			static_cast<unsigned char>(*p) - k_first_char,
			&x,
			&y,
			&q,
			1
		);
	}
	return x;
}

float hud_line_height()
{
	return g_ascent - g_descent + g_line_gap;
}

float hud_ascent()
{
	return g_ascent;
}

float hud_descent()
{
	return g_descent;
}

float hud_draw_text(const char* text, float pen_x, float pen_y, uint32_t abgr)
{
	if (!g_initialized || !text || !*text || !bgfx::isValid(g_program) || !bgfx::isValid(g_atlas_tex)) {
		return 0.0f;
	}

	const size_t glyph_count = std::strlen(text);
	if (glyph_count == 0) {
		return 0.0f;
	}

	const uint32_t max_quads = static_cast<uint32_t>(glyph_count);
	const uint32_t max_verts = max_quads * 4;
	const uint32_t max_indices = max_quads * 6;

	if (bgfx::getAvailTransientVertexBuffer(max_verts, g_layout) < max_verts
	    || bgfx::getAvailTransientIndexBuffer(max_indices) < max_indices) {
		return 0.0f;
	}

	bgfx::TransientVertexBuffer tvb;
	bgfx::TransientIndexBuffer tib;
	bgfx::allocTransientVertexBuffer(&tvb, max_verts, g_layout);
	bgfx::allocTransientIndexBuffer(&tib, max_indices);

	HudVertex* vertices = reinterpret_cast<HudVertex*>(tvb.data);
	uint16_t* indices = reinterpret_cast<uint16_t*>(tib.data);

	float x = pen_x;
	float y = pen_y;
	uint32_t quads_emitted = 0;

	for (const char* p = text; *p; ++p) {
		const unsigned char ch = static_cast<unsigned char>(*p);
		if (ch < k_first_char || ch >= k_first_char + k_char_count) {
			continue;
		}
		stbtt_aligned_quad q;
		stbtt_GetBakedQuad(
			g_chars.data(),
			k_atlas_w,
			k_atlas_h,
			ch - k_first_char,
			&x,
			&y,
			&q,
			1
		);

		HudVertex* v = &vertices[quads_emitted * 4];
		v[0] = {q.x0, q.y0, 0.0f, q.s0, q.t0, abgr};
		v[1] = {q.x1, q.y0, 0.0f, q.s1, q.t0, abgr};
		v[2] = {q.x1, q.y1, 0.0f, q.s1, q.t1, abgr};
		v[3] = {q.x0, q.y1, 0.0f, q.s0, q.t1, abgr};

		uint16_t* idx = &indices[quads_emitted * 6];
		const uint16_t base = static_cast<uint16_t>(quads_emitted * 4);
		idx[0] = base + 0;
		idx[1] = base + 1;
		idx[2] = base + 2;
		idx[3] = base + 0;
		idx[4] = base + 2;
		idx[5] = base + 3;

		++quads_emitted;
	}

	if (quads_emitted == 0) {
		return 0.0f;
	}

	const uint64_t state = BGFX_STATE_WRITE_RGB
		| BGFX_STATE_WRITE_A
		| BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);

	bgfx::setState(state);
	bgfx::setTexture(0, g_s_albedo, g_atlas_tex);
	bgfx::setVertexBuffer(0, &tvb, 0, quads_emitted * 4);
	bgfx::setIndexBuffer(&tib, 0, quads_emitted * 6);
	bgfx::submit(g_view, g_program);

	return x - pen_x;
}

float hud_draw_text_right(const char* text, float right_x, float pen_y, uint32_t abgr)
{
	const float width = hud_text_width(text);
	return hud_draw_text(text, right_x - width, pen_y, abgr);
}

void hud_draw_solid_rect(float x, float y, float w, float h, uint32_t abgr)
{
	if (!g_initialized || !bgfx::isValid(g_program) || !bgfx::isValid(g_atlas_tex)) {
		return;
	}
	if (w <= 0.0f || h <= 0.0f) {
		return;
	}

	if (bgfx::getAvailTransientVertexBuffer(4, g_layout) < 4
	    || bgfx::getAvailTransientIndexBuffer(6) < 6) {
		return;
	}

	bgfx::TransientVertexBuffer tvb;
	bgfx::TransientIndexBuffer tib;
	bgfx::allocTransientVertexBuffer(&tvb, 4, g_layout);
	bgfx::allocTransientIndexBuffer(&tib, 6);

	// UV at the center of the reserved bottom-right texel (always alpha=255).
	const float u = 1.0f - 0.5f / static_cast<float>(k_atlas_w);
	const float v = 1.0f - 0.5f / static_cast<float>(k_atlas_h);

	HudVertex* verts = reinterpret_cast<HudVertex*>(tvb.data);
	verts[0] = {x,         y,         0.0f, u, v, abgr};
	verts[1] = {x + w,     y,         0.0f, u, v, abgr};
	verts[2] = {x + w,     y + h,     0.0f, u, v, abgr};
	verts[3] = {x,         y + h,     0.0f, u, v, abgr};

	uint16_t* idx = reinterpret_cast<uint16_t*>(tib.data);
	idx[0] = 0; idx[1] = 1; idx[2] = 2;
	idx[3] = 0; idx[4] = 2; idx[5] = 3;

	const uint64_t state = BGFX_STATE_WRITE_RGB
		| BGFX_STATE_WRITE_A
		| BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);

	bgfx::setState(state);
	bgfx::setTexture(0, g_s_albedo, g_atlas_tex);
	bgfx::setVertexBuffer(0, &tvb);
	bgfx::setIndexBuffer(&tib);
	bgfx::submit(g_view, g_program);
}

} // namespace engine
