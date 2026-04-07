#include "ecs.hpp"
#include "fps_camera.hpp"
#include "level_loader.hpp"
#include "lit_vertex.hpp"
#include "physics_world.hpp"
#include "shader_program.hpp"
#include "texture_loader.hpp"

#include <SDL.h>
#include <SDL_syswm.h>

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#include <bx/math.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#ifndef ENGINE_TEXTURES_DIR
#define ENGINE_TEXTURES_DIR "textures"
#endif

#ifndef ENGINE_MAPS_DIR
#define ENGINE_MAPS_DIR "maps"
#endif

namespace {

void* native_window_handle(SDL_Window* window)
{
	SDL_SysWMinfo wmi;
	SDL_VERSION(&wmi.version);
	if (!SDL_GetWindowWMInfo(window, &wmi)) {
		return nullptr;
	}

#if defined(SDL_VIDEO_DRIVER_WINDOWS)
	return reinterpret_cast<void*>(wmi.info.win.window);
#elif defined(SDL_VIDEO_DRIVER_COCOA)
	return wmi.info.cocoa.window;
#elif defined(SDL_VIDEO_DRIVER_X11)
	return reinterpret_cast<void*>(static_cast<uintptr_t>(wmi.info.x11.window));
#elif defined(SDL_VIDEO_DRIVER_WAYLAND)
	return wmi.info.wl.surface;
#else
	(void)window;
	return nullptr;
#endif
}

void* native_display_handle(SDL_Window* window)
{
	SDL_SysWMinfo wmi;
	SDL_VERSION(&wmi.version);
	if (!SDL_GetWindowWMInfo(window, &wmi)) {
		return nullptr;
	}

#if defined(SDL_VIDEO_DRIVER_X11)
	return wmi.info.x11.display;
#else
	(void)window;
	return nullptr;
#endif
}

void window_pixel_size(SDL_Window* window, int* out_w, int* out_h)
{
#if SDL_VERSION_ATLEAST(2, 26, 0)
	SDL_GetWindowSizeInPixels(window, out_w, out_h);
#else
	SDL_GetWindowSize(window, out_w, out_h);
#endif
}

static constexpr float k_sphere_radius = 1.35f;
static constexpr uint32_t k_sphere_color_abgr = 0xff8877ddu;
static constexpr float k_wall_height = 3.2f;

static void build_uv_sphere(
	std::vector<LitVertex>& vertices,
	std::vector<uint16_t>& indices,
	float radius,
	int stacks,
	int slices,
	uint32_t abgr
)
{
	vertices.clear();
	indices.clear();
	vertices.reserve(static_cast<size_t>(stacks + 1) * static_cast<size_t>(slices));
	for (int i = 0; i <= stacks; ++i) {
		const float phi = -bx::kPiHalf + (bx::kPi * static_cast<float>(i) / static_cast<float>(stacks));
		const float cos_phi = std::cos(phi);
		const float sin_phi = std::sin(phi);
		for (int j = 0; j < slices; ++j) {
			const float theta = (bx::kPi * 2.0f) * static_cast<float>(j) / static_cast<float>(slices);
			const float cos_theta = std::cos(theta);
			const float sin_theta = std::sin(theta);
			LitVertex v;
			v.x = radius * cos_phi * cos_theta;
			v.y = radius * sin_phi;
			v.z = radius * cos_phi * sin_theta;
			v.nx = cos_phi * cos_theta;
			v.ny = sin_phi;
			v.nz = cos_phi * sin_theta;
			v.u = static_cast<float>(j) / static_cast<float>(slices);
			v.v = static_cast<float>(i) / static_cast<float>(stacks);
			v.abgr = abgr;
			vertices.push_back(v);
		}
	}
	for (int i = 0; i < stacks; ++i) {
		for (int j = 0; j < slices; ++j) {
			const int jn = (j + 1) % slices;
			const uint16_t a = static_cast<uint16_t>(i * slices + j);
			const uint16_t b = static_cast<uint16_t>((i + 1) * slices + j);
			const uint16_t c = static_cast<uint16_t>((i + 1) * slices + jn);
			const uint16_t d = static_cast<uint16_t>(i * slices + jn);
			indices.push_back(a);
			indices.push_back(b);
			indices.push_back(c);
			indices.push_back(a);
			indices.push_back(c);
			indices.push_back(d);
		}
	}
}

bool first_floor_spawn(const engine::LoadedLevel& level, float& out_x, float& out_z)
{
	for (int r = 0; r < level.height; ++r) {
		for (int c = 0; c < level.width; ++c) {
			if (level.is_floor(c, r)) {
				level.cell_center_world(c, r, out_x, out_z);
				return true;
			}
		}
	}
	return false;
}

bool floor_centroid(const engine::LoadedLevel& level, float& out_x, float& out_z)
{
	double sx = 0.0;
	double sz = 0.0;
	int n = 0;
	for (int r = 0; r < level.height; ++r) {
		for (int c = 0; c < level.width; ++c) {
			if (level.is_floor(c, r)) {
				float wx = 0.0f;
				float wz = 0.0f;
				level.cell_center_world(c, r, wx, wz);
				sx += static_cast<double>(wx);
				sz += static_cast<double>(wz);
				++n;
			}
		}
	}
	if (n == 0) {
		return false;
	}
	out_x = static_cast<float>(sx / static_cast<double>(n));
	out_z = static_cast<float>(sz / static_cast<double>(n));
	return true;
}

void nearest_light(
	const engine::LoadedLevel& level,
	float ex,
	float ey,
	float ez,
	float out_pos[4]
)
{
	const size_t n = level.light_positions.size() / 3;
	if (n == 0) {
		out_pos[0] = 0.0f;
		out_pos[1] = 4.0f;
		out_pos[2] = 0.0f;
		out_pos[3] = 0.0f;
		return;
	}
	float best = std::numeric_limits<float>::max();
	size_t best_i = 0;
	for (size_t i = 0; i < n; ++i) {
		const float lx = level.light_positions[i * 3 + 0];
		const float ly = level.light_positions[i * 3 + 1];
		const float lz = level.light_positions[i * 3 + 2];
		const float dx = lx - ex;
		const float dy = ly - ey;
		const float dz = lz - ez;
		const float d2 = dx * dx + dy * dy + dz * dz;
		if (d2 < best) {
			best = d2;
			best_i = i;
		}
	}
	out_pos[0] = level.light_positions[best_i * 3 + 0];
	out_pos[1] = level.light_positions[best_i * 3 + 1];
	out_pos[2] = level.light_positions[best_i * 3 + 2];
	out_pos[3] = 0.0f;
}

} // namespace

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
		std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}

	SDL_Window* window = SDL_CreateWindow(
		"fps-engine (SDL2 + bgfx)",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		1280,
		720,
		SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
	);
	if (!window) {
		std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	bgfx::PlatformData pd{};
	pd.nwh = native_window_handle(window);
	pd.ndt = native_display_handle(window);
	if (!pd.nwh) {
		std::fprintf(stderr, "Native window handle is null (SDL video driver unsupported?)\n");
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	int width = 0;
	int height = 0;
	window_pixel_size(window, &width, &height);

	bgfx::Init init;
	init.platformData = pd;
	init.resolution.width = static_cast<uint32_t>(width);
	init.resolution.height = static_cast<uint32_t>(height);
	init.resolution.reset = BGFX_RESET_VSYNC;
	if (!bgfx::init(init)) {
		std::fprintf(stderr, "bgfx::init failed\n");
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	bgfx::setDebug(BGFX_DEBUG_TEXT);
	bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x1a1a2eff, 1.0f, 0);
	bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(width), static_cast<uint16_t>(height));

	engine::LoadedLevel level;
	std::string level_err;
	if (!engine::load_evil_level(
			ENGINE_MAPS_DIR "/example.evil",
			ENGINE_MAPS_DIR "/example_lights.evil",
			level,
			level_err
		)) {
		std::fprintf(stderr, "load_evil_level: %s\n", level_err.c_str());
		bgfx::shutdown();
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	std::vector<LitVertex> floor_vertices;
	std::vector<LitVertex> wall_vertices;
	engine::build_level_meshes(level, k_wall_height, floor_vertices, wall_vertices);
	if (floor_vertices.empty()) {
		std::fprintf(stderr, "level has no floor tiles\n");
		bgfx::shutdown();
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	bgfx::VertexLayout layout;
	layout.begin()
		.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
		.add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
		.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
		.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
		.end();

	const bgfx::Memory* floor_vb_mem = bgfx::copy(
		floor_vertices.data(),
		static_cast<uint32_t>(floor_vertices.size() * sizeof(LitVertex))
	);
	const bgfx::VertexBufferHandle floor_vbh = bgfx::createVertexBuffer(floor_vb_mem, layout);

	bgfx::VertexBufferHandle wall_vbh = BGFX_INVALID_HANDLE;
	if (!wall_vertices.empty()) {
		const bgfx::Memory* wall_vb_mem = bgfx::copy(
			wall_vertices.data(),
			static_cast<uint32_t>(wall_vertices.size() * sizeof(LitVertex))
		);
		wall_vbh = bgfx::createVertexBuffer(wall_vb_mem, layout);
	}

	const bgfx::ProgramHandle program = engine::load_triangle_program();
	if (!bgfx::isValid(program)) {
		std::fprintf(stderr, "Shader program creation failed\n");
		if (bgfx::isValid(wall_vbh)) {
			bgfx::destroy(wall_vbh);
		}
		bgfx::destroy(floor_vbh);
		bgfx::shutdown();
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	const bgfx::UniformHandle u_light_pos = bgfx::createUniform("u_lightPos", bgfx::UniformType::Vec4);
	const bgfx::UniformHandle u_light_color = bgfx::createUniform("u_lightColor", bgfx::UniformType::Vec4);
	const bgfx::UniformHandle u_ambient = bgfx::createUniform("u_ambient", bgfx::UniformType::Vec4);
	const bgfx::UniformHandle s_albedo = bgfx::createUniform("s_albedo", bgfx::UniformType::Sampler);

	static const uint32_t k_white_rgba = 0xffffffffu;
	const bgfx::Memory* white_mem = bgfx::copy(&k_white_rgba, sizeof(k_white_rgba));
	const bgfx::TextureHandle white_tex = bgfx::createTexture2D(
		1,
		1,
		false,
		1,
		bgfx::TextureFormat::RGBA8,
		0,
		white_mem
	);

	const bgfx::TextureHandle floor_tex = engine::load_texture_from_file(
		ENGINE_TEXTURES_DIR "/checkered_pavement_tiles_diff_2k.jpg"
	);
	const bgfx::TextureHandle floor_bind = bgfx::isValid(floor_tex) ? floor_tex : white_tex;

	const bgfx::TextureHandle wall_tex = engine::load_texture_from_file(
		ENGINE_TEXTURES_DIR "/plastered_wall_04_diff_2k.jpg"
	);
	const bgfx::TextureHandle wall_bind = bgfx::isValid(wall_tex) ? wall_tex : white_tex;

	std::vector<LitVertex> sphere_vertices;
	std::vector<uint16_t> sphere_indices;
	build_uv_sphere(sphere_vertices, sphere_indices, k_sphere_radius, 28, 40, k_sphere_color_abgr);
	const bgfx::Memory* sphere_vb_mem = bgfx::copy(
		sphere_vertices.data(),
		static_cast<uint32_t>(sphere_vertices.size() * sizeof(LitVertex))
	);
	const bgfx::Memory* sphere_ib_mem = bgfx::copy(
		sphere_indices.data(),
		static_cast<uint32_t>(sphere_indices.size() * sizeof(uint16_t))
	);
	const bgfx::VertexBufferHandle sphere_vbh = bgfx::createVertexBuffer(sphere_vb_mem, layout);
	const bgfx::IndexBufferHandle sphere_ibh = bgfx::createIndexBuffer(sphere_ib_mem);

	engine::Registry registry;
	engine::ecs_bootstrap(registry);
	(void)registry;

	FpsCamera camera;
	float spawn_x = 0.0f;
	float spawn_z = 0.0f;
	if (!first_floor_spawn(level, spawn_x, spawn_z)) {
		spawn_x = 0.0f;
		spawn_z = 0.0f;
	}
	camera.eyeX = spawn_x;
	camera.eyeY = engine::PlayerPhysics::k_eye_height + engine::k_platform_surface_y + 0.02f;
	camera.eyeZ = spawn_z;

	float sphere_cx = 0.0f;
	float sphere_cz = 0.0f;
	if (!floor_centroid(level, sphere_cx, sphere_cz)) {
		sphere_cx = spawn_x;
		sphere_cz = spawn_z;
	}

	engine::PlayerPhysics player_physics;
	bool mouseLook = true;
	if (SDL_SetRelativeMouseMode(mouseLook ? SDL_TRUE : SDL_FALSE) != 0) {
		std::fprintf(stderr, "SDL_SetRelativeMouseMode: %s\n", SDL_GetError());
	}

	uint64_t prevTicks = SDL_GetTicks64();
	bool running = true;
	while (running) {
		float mouseRelX = 0.0f;
		float mouseRelY = 0.0f;

		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) {
				running = false;
			} else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
				mouseLook = !mouseLook;
				SDL_SetRelativeMouseMode(mouseLook ? SDL_TRUE : SDL_FALSE);
			} else if (event.type == SDL_MOUSEMOTION && mouseLook) {
				mouseRelX += static_cast<float>(event.motion.xrel);
				mouseRelY += static_cast<float>(event.motion.yrel);
			} else if (event.type == SDL_WINDOWEVENT
				&& (event.window.event == SDL_WINDOWEVENT_RESIZED
					|| event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
				window_pixel_size(window, &width, &height);
				bgfx::reset(static_cast<uint32_t>(width), static_cast<uint32_t>(height), BGFX_RESET_VSYNC);
				bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(width), static_cast<uint16_t>(height));
			}
		}

		const uint64_t nowTicks = SDL_GetTicks64();
		float dt = static_cast<float>(nowTicks - prevTicks) * 0.001f;
		prevTicks = nowTicks;
		if (dt > 0.1f) {
			dt = 0.1f;
		}

		if (mouseLook) {
			fps_camera_apply_mouse(camera, mouseRelX, mouseRelY, 0.0025f);
		}

		const float prev_eye_x = camera.eyeX;
		const float prev_eye_z = camera.eyeZ;

		const uint8_t* keys = SDL_GetKeyboardState(nullptr);
		float dx = 0.0f;
		float dy = 0.0f;
		float dz = 0.0f;
		fps_camera_apply_wasd(camera, keys, dt, 5.0f, dx, dy, dz);
		camera.eyeX += dx;
		camera.eyeY += dy;
		camera.eyeZ += dz;

		engine::player_physics_step(camera, player_physics, dt, level, prev_eye_x, prev_eye_z);

		const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
		float view[16];
		float proj[16];
		fps_camera_view_proj(camera, aspect, bgfx::getCaps()->homogeneousDepth, view, proj);
		bgfx::setViewTransform(0, view, proj);

		bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(width), static_cast<uint16_t>(height));
		bgfx::touch(0);

		bgfx::dbgTextClear();
		bgfx::dbgTextPrintf(0, 1, 0x0f, "WASD  Mouse  Esc: %s   .evil level",
			mouseLook ? "free cursor" : "capture");

		float light_pos[4];
		nearest_light(level, camera.eyeX, camera.eyeY, camera.eyeZ, light_pos);
		const float light_color[4] = {2.4f, 2.1f, 1.7f, 0.0f};
		const float ambient[4] = {0.07f, 0.08f, 0.11f, 0.0f};
		bgfx::setUniform(u_light_pos, light_pos);
		bgfx::setUniform(u_light_color, light_color);
		bgfx::setUniform(u_ambient, ambient);

		float model[16];
		const uint64_t renderState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
			| BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA;

		bgfx::setState(renderState);
		bx::mtxIdentity(model);
		bgfx::setTransform(model);
		bgfx::setTexture(0, s_albedo, floor_bind);
		bgfx::setVertexBuffer(0, floor_vbh);
		bgfx::submit(0, program);

		if (bgfx::isValid(wall_vbh)) {
			bx::mtxIdentity(model);
			bgfx::setTransform(model);
			bgfx::setTexture(0, s_albedo, wall_bind);
			bgfx::setVertexBuffer(0, wall_vbh);
			bgfx::submit(0, program);
		}

		bx::mtxTranslate(model, sphere_cx, k_sphere_radius, sphere_cz);
		bgfx::setTransform(model);
		bgfx::setTexture(0, s_albedo, white_tex);
		bgfx::setVertexBuffer(0, sphere_vbh);
		bgfx::setIndexBuffer(sphere_ibh);
		bgfx::submit(0, program);

		bgfx::frame();
	}

	if (mouseLook) {
		SDL_SetRelativeMouseMode(SDL_FALSE);
	}

	if (bgfx::isValid(floor_tex)) {
		bgfx::destroy(floor_tex);
	}
	if (bgfx::isValid(wall_tex)) {
		bgfx::destroy(wall_tex);
	}
	bgfx::destroy(white_tex);
	bgfx::destroy(s_albedo);
	bgfx::destroy(u_ambient);
	bgfx::destroy(u_light_color);
	bgfx::destroy(u_light_pos);
	bgfx::destroy(program);
	bgfx::destroy(sphere_ibh);
	bgfx::destroy(sphere_vbh);
	if (bgfx::isValid(wall_vbh)) {
		bgfx::destroy(wall_vbh);
	}
	bgfx::destroy(floor_vbh);
	bgfx::shutdown();

	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
