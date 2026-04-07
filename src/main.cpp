#include "ecs.hpp"
#include "fps_camera.hpp"
#include "physics_world.hpp"
#include "shader_program.hpp"

#include <SDL.h>
#include <SDL_syswm.h>

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#include <bx/math.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

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

struct LitVertex {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float nx = 0.0f;
	float ny = 1.0f;
	float nz = 0.0f;
	uint32_t abgr = 0xffffffff;
};

static LitVertex k_triangle_vertices[] = {
	{0.0f, 1.35f, 0.0f, 0.0f, 0.0f, 1.0f, 0xff3355ffu},
	{-0.55f, 0.2f, 0.0f, 0.0f, 0.0f, 1.0f, 0xff99ff55u},
	{0.55f, 0.2f, 0.0f, 0.0f, 0.0f, 1.0f, 0xff44ccffu},
};

static constexpr uint32_t k_platform_color_abgr = 0xff6a5c48u;

static LitVertex k_platform_vertices[] = {
	{-engine::k_platform_half_extent, engine::k_platform_surface_y, -engine::k_platform_half_extent, 0.0f, 1.0f, 0.0f, k_platform_color_abgr},
	{ engine::k_platform_half_extent, engine::k_platform_surface_y,  engine::k_platform_half_extent, 0.0f, 1.0f, 0.0f, k_platform_color_abgr},
	{ engine::k_platform_half_extent, engine::k_platform_surface_y, -engine::k_platform_half_extent, 0.0f, 1.0f, 0.0f, k_platform_color_abgr},
	{-engine::k_platform_half_extent, engine::k_platform_surface_y, -engine::k_platform_half_extent, 0.0f, 1.0f, 0.0f, k_platform_color_abgr},
	{-engine::k_platform_half_extent, engine::k_platform_surface_y,  engine::k_platform_half_extent, 0.0f, 1.0f, 0.0f, k_platform_color_abgr},
	{ engine::k_platform_half_extent, engine::k_platform_surface_y,  engine::k_platform_half_extent, 0.0f, 1.0f, 0.0f, k_platform_color_abgr},
};

static constexpr float k_sphere_radius = 1.35f;
static constexpr uint32_t k_sphere_color_abgr = 0xff8877ddu;

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

	bgfx::VertexLayout layout;
	layout.begin()
		.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
		.add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
		.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
		.end();

	const bgfx::Memory* vb_mem = bgfx::copy(k_triangle_vertices, sizeof(k_triangle_vertices));
	const bgfx::VertexBufferHandle vbh = bgfx::createVertexBuffer(vb_mem, layout);

	const bgfx::Memory* platform_vb_mem = bgfx::copy(k_platform_vertices, sizeof(k_platform_vertices));
	const bgfx::VertexBufferHandle platform_vbh = bgfx::createVertexBuffer(platform_vb_mem, layout);

	const bgfx::ProgramHandle program = engine::load_triangle_program();
	if (!bgfx::isValid(program)) {
		std::fprintf(stderr, "Shader program creation failed\n");
		bgfx::destroy(platform_vbh);
		bgfx::destroy(vbh);
		bgfx::shutdown();
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	const bgfx::UniformHandle u_light_pos = bgfx::createUniform("u_lightPos", bgfx::UniformType::Vec4);
	const bgfx::UniformHandle u_light_color = bgfx::createUniform("u_lightColor", bgfx::UniformType::Vec4);
	const bgfx::UniformHandle u_ambient = bgfx::createUniform("u_ambient", bgfx::UniformType::Vec4);

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

		const uint8_t* keys = SDL_GetKeyboardState(nullptr);
		float dx = 0.0f;
		float dy = 0.0f;
		float dz = 0.0f;
		fps_camera_apply_wasd(camera, keys, dt, 5.0f, dx, dy, dz);
		camera.eyeX += dx;
		camera.eyeY += dy;
		camera.eyeZ += dz;

		engine::player_physics_step(camera, player_physics, dt);

		const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
		float view[16];
		float proj[16];
		fps_camera_view_proj(camera, aspect, bgfx::getCaps()->homogeneousDepth, view, proj);
		bgfx::setViewTransform(0, view, proj);

		bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(width), static_cast<uint16_t>(height));
		bgfx::touch(0);

		bgfx::dbgTextClear();
		bgfx::dbgTextPrintf(0, 1, 0x0f, "WASD  Mouse  Esc: %s   Point light + gravity",
			mouseLook ? "free cursor" : "capture");

		const float light_pos[4] = {2.8f, 5.5f, 1.8f, 0.0f};
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
		bgfx::setVertexBuffer(0, platform_vbh);
		bgfx::submit(0, program);

		bx::mtxIdentity(model);
		bgfx::setTransform(model);
		bgfx::setVertexBuffer(0, vbh);
		bgfx::submit(0, program);

		bx::mtxTranslate(model, 0.0f, k_sphere_radius, 0.0f);
		bgfx::setTransform(model);
		bgfx::setVertexBuffer(0, sphere_vbh);
		bgfx::setIndexBuffer(sphere_ibh);
		bgfx::submit(0, program);

		bgfx::frame();
	}

	if (mouseLook) {
		SDL_SetRelativeMouseMode(SDL_FALSE);
	}

	bgfx::destroy(u_ambient);
	bgfx::destroy(u_light_color);
	bgfx::destroy(u_light_pos);
	bgfx::destroy(program);
	bgfx::destroy(sphere_ibh);
	bgfx::destroy(sphere_vbh);
	bgfx::destroy(platform_vbh);
	bgfx::destroy(vbh);
	bgfx::shutdown();

	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
