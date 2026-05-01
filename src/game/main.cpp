#include "engine/application/application.hpp"
#include "engine/renderer/renderer.hpp"
#include "engine/window.hpp"
#include "engine/audio.hpp"
#include "game/fps_camera.hpp"
#include "engine/geometry/primitives.hpp"
#include "engine/hud.hpp"
#include "game/level/level_data.hpp"
#include "game/level/level_loader.hpp"
#include "game/level/level_mesh.hpp"
#include "engine/lit_vertex.hpp"
#include "engine/physics/raycast.hpp"
#include "game/physics_world.hpp"
#include "engine/render/buffers.hpp"
#include "engine/shader_program.hpp"
#include "engine/texture_loader.hpp"
#include "game/viewmodel.hpp"

#include <SDL.h>

#include <bgfx/bgfx.h>

#include <bx/math.h>

#include <algorithm>
#include <array>
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

#ifndef ENGINE_MODELS_DIR
#define ENGINE_MODELS_DIR "models"
#endif

#ifndef ENGINE_SOUNDS_DIR
#define ENGINE_SOUNDS_DIR "sounds"
#endif

#ifndef ENGINE_FONTS_DIR
#define ENGINE_FONTS_DIR "fonts"
#endif

namespace {

static constexpr float k_light_bulb_radius = 0.14f;
static constexpr uint32_t k_light_bulb_abgr = 0xffffffffu;
static constexpr int k_max_shader_lights = 8;

struct Target {
  float pos_x;
  float pos_y; // center y
  float pos_z;
  float half_extent; // cube half-side
  int hits_remaining;
  bool alive;
};

constexpr int k_target_max_hits = 3;

} // namespace

int main(int argc, char **argv) {
  const char *level_path = ENGINE_MAPS_DIR "/mansion.json";
  if (argc >= 2) {
    level_path = argv[1];
  }

  Application app;
  if (!app.init())
    return 1;

  Window window;
  if (!window.create("fps-engine (SDL2 + bgfx)", 1280, 720)) {
    std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    return 1;
  }

  Renderer renderer;
  if (!renderer.init(window))
    return 1;

  int width = renderer.width();
  int height = renderer.height();

  engine::SoundId shot_sound = engine::k_invalid_sound;
  engine::SoundId step_sound = engine::k_invalid_sound;
  {
    std::string audio_err;
    if (!engine::audio_init(audio_err)) {
      std::fprintf(stderr, "audio: %s (continuing without sound)\n",
                   audio_err.c_str());
    } else {
      shot_sound = engine::audio_load(ENGINE_SOUNDS_DIR "/pistol_shot.mp3", audio_err);
      if (shot_sound == engine::k_invalid_sound)
        std::fprintf(stderr, "audio: %s\n", audio_err.c_str());
      step_sound = engine::audio_load(ENGINE_SOUNDS_DIR "/footsteps.mp3", audio_err);
      if (step_sound == engine::k_invalid_sound)
        std::fprintf(stderr, "audio: %s\n", audio_err.c_str());
    }
  }

  engine::Level level;
  std::string level_err;
  if (!engine::load_level_any(level_path, level, level_err)) {
    std::fprintf(stderr, "load_level: %s\n", level_err.c_str());
    return 1;
  }

  engine::LevelMeshOutput meshes;
  engine::build_level_meshes(level, meshes);
  if (meshes.floor_vertices.empty()) {
    std::fprintf(stderr, "level has no floor geometry (sectors empty?)\n");
    return 1;
  }

  bgfx::VertexLayout layout;
  layout.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .end();

  const bgfx::VertexBufferHandle floor_vbh =
      engine::create_vertex_buffer(meshes.floor_vertices, layout);
  const bgfx::VertexBufferHandle wall_vbh =
      engine::create_vertex_buffer(meshes.wall_vertices, layout);
  const bgfx::VertexBufferHandle stair_vbh =
      engine::create_vertex_buffer(meshes.stair_vertices, layout);

  const bgfx::ProgramHandle program = engine::load_triangle_program();
  if (!bgfx::isValid(program)) {
    std::fprintf(stderr, "Shader program creation failed\n");
    if (bgfx::isValid(stair_vbh))
      bgfx::destroy(stair_vbh);
    if (bgfx::isValid(wall_vbh))
      bgfx::destroy(wall_vbh);
    if (bgfx::isValid(floor_vbh))
      bgfx::destroy(floor_vbh);
    return 1;
  }

  const bgfx::ProgramHandle skinned_program = engine::load_skinned_program();
  if (!bgfx::isValid(skinned_program)) {
    std::fprintf(stderr, "Skinned program creation failed\n");
  }

  const bgfx::ProgramHandle debug_program = engine::load_debug_program();
  if (!bgfx::isValid(debug_program)) {
    std::fprintf(stderr, "Debug program creation failed\n");
  }

  const bgfx::ProgramHandle hud_program = engine::load_hud_program();
  if (!bgfx::isValid(hud_program)) {
    std::fprintf(stderr, "HUD program creation failed\n");
  }

  bool hud_ok = false;
  if (bgfx::isValid(hud_program)) {
    engine::HudInitDesc hud_desc;
    hud_desc.font_path =
        ENGINE_FONTS_DIR "/moms_typewriter/moms_typewriter.ttf";
    hud_desc.pixel_height = 36.0f;
    hud_desc.program = hud_program;
    std::string hud_err;
    if (!engine::hud_init(hud_desc, hud_err)) {
      std::fprintf(stderr, "hud: %s (continuing without HUD)\n",
                   hud_err.c_str());
    } else {
      hud_ok = true;
    }
  }

  int player_health = 100;
  int bullets_in_clip = 8;
  const int clip_size = 8;
  bool is_reloading = false;
  bool is_walking = false;

  const bgfx::UniformHandle u_light_pos = bgfx::createUniform(
      "u_lightPos", bgfx::UniformType::Vec4, k_max_shader_lights);
  const bgfx::UniformHandle u_light_color = bgfx::createUniform(
      "u_lightColor", bgfx::UniformType::Vec4, k_max_shader_lights);
  const bgfx::UniformHandle u_light_params =
      bgfx::createUniform("u_lightParams", bgfx::UniformType::Vec4);
  const bgfx::UniformHandle u_ambient =
      bgfx::createUniform("u_ambient", bgfx::UniformType::Vec4);
  const bgfx::UniformHandle s_albedo =
      bgfx::createUniform("s_albedo", bgfx::UniformType::Sampler);
  const bgfx::UniformHandle u_bones =
      bgfx::createUniform("u_bones", bgfx::UniformType::Mat4, 120);
  const bgfx::UniformHandle u_baseColor =
      bgfx::createUniform("u_baseColor", bgfx::UniformType::Vec4);

  static const uint32_t k_white_rgba = 0xffffffffu;
  const bgfx::Memory *white_mem =
      bgfx::copy(&k_white_rgba, sizeof(k_white_rgba));
  const bgfx::TextureHandle white_tex = bgfx::createTexture2D(
      1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, white_mem);

  const bgfx::TextureHandle floor_tex = engine::load_texture_from_file(
      ENGINE_TEXTURES_DIR "/checkered_pavement_tiles_diff_2k.jpg");
  const bgfx::TextureHandle floor_bind =
      bgfx::isValid(floor_tex) ? floor_tex : white_tex;

  const bgfx::TextureHandle wall_tex = engine::load_texture_from_file(
      ENGINE_TEXTURES_DIR "/plastered_wall_04_diff_2k.jpg");
  const bgfx::TextureHandle wall_bind =
      bgfx::isValid(wall_tex) ? wall_tex : white_tex;

  std::vector<LitVertex> bulb_vertices;
  std::vector<uint16_t> bulb_indices;
  engine::build_uv_sphere(bulb_vertices, bulb_indices, k_light_bulb_radius, 10,
                          14, k_light_bulb_abgr);
  const bgfx::Memory *bulb_vb_mem = bgfx::copy(
      bulb_vertices.data(),
      static_cast<uint32_t>(bulb_vertices.size() * sizeof(LitVertex)));
  const bgfx::Memory *bulb_ib_mem =
      bgfx::copy(bulb_indices.data(),
                 static_cast<uint32_t>(bulb_indices.size() * sizeof(uint16_t)));
  const bgfx::VertexBufferHandle bulb_vbh =
      bgfx::createVertexBuffer(bulb_vb_mem, layout);
  const bgfx::IndexBufferHandle bulb_ibh = bgfx::createIndexBuffer(bulb_ib_mem);

  // Three target cubes at three damage tints. We swap which VBH is bound based
  // on hits_remaining so each shot visibly darkens the cube before it
  // disappears.
  constexpr float k_target_half_extent = 0.4f;
  constexpr uint32_t k_target_tints[k_target_max_hits] = {
      0xff5050ffu, // bright red (full health)
      0xff3030c0u, // mid red    (1 hit)
      0xff1a1a80u, // dark red   (2 hits, next shot kills)
  };
  std::vector<LitVertex> cube_verts;
  std::vector<uint16_t> cube_indices;
  bgfx::VertexBufferHandle cube_vbh[k_target_max_hits];
  bgfx::IndexBufferHandle cube_ibh = BGFX_INVALID_HANDLE;
  for (int t = 0; t < k_target_max_hits; ++t) {
    engine::build_unit_cube(cube_verts, cube_indices, k_target_half_extent,
                            k_target_tints[t]);
    const bgfx::Memory *vb_mem =
        bgfx::copy(cube_verts.data(), static_cast<uint32_t>(cube_verts.size() *
                                                            sizeof(LitVertex)));
    cube_vbh[t] = bgfx::createVertexBuffer(vb_mem, layout);
    if (t == 0) {
      const bgfx::Memory *ib_mem = bgfx::copy(
          cube_indices.data(),
          static_cast<uint32_t>(cube_indices.size() * sizeof(uint16_t)));
      cube_ibh = bgfx::createIndexBuffer(ib_mem);
    }
  }

  std::vector<Target> targets = {
      {3.0f, k_target_half_extent, 5.0f, k_target_half_extent,
       k_target_max_hits, true},
      {4.0f, k_target_half_extent, 5.5f, k_target_half_extent,
       k_target_max_hits, true},
      {5.0f, k_target_half_extent, 5.0f, k_target_half_extent,
       k_target_max_hits, true},
  };

  engine::Viewmodel viewmodel;
  {
    std::string vm_err;
    if (!viewmodel.load(ENGINE_MODELS_DIR "/animated_pistol-v2.glb", vm_err)) {
      std::fprintf(stderr, "viewmodel load: %s\n", vm_err.c_str());
    } else {
      // Prefer Take on first equip, but fall back to Idle (re-exported GLBs
      // from Blender often ship with only the active action — Take is a no-op
      // then).
      viewmodel.play(engine::ViewmodelAnim::Take, false, true);
      viewmodel.play(engine::ViewmodelAnim::Idle, true, false);
    }
  }

  FpsCamera camera;
  camera.eyeX = level.spawn.pos.x;
  camera.eyeY = level.spawn.pos.y + engine::PlayerPhysics::k_eye_height + 0.02f;
  camera.eyeZ = level.spawn.pos.z;
  camera.yaw = level.spawn.yaw_deg * (bx::kPi / 180.0f);

  engine::PlayerPhysics player_physics;
  bool mouseLook = true;
  bool show_axes_gizmo = false;
  if (SDL_SetRelativeMouseMode(mouseLook ? SDL_TRUE : SDL_FALSE) != 0) {
    std::fprintf(stderr, "SDL_SetRelativeMouseMode: %s\n", SDL_GetError());
  }

  uint64_t prevTicks = SDL_GetTicks64();
  while (window.is_running()) {
    float mouseRelX = 0.0f;
    float mouseRelY = 0.0f;
    bool shoot_pressed = false;
    bool reload_pressed = false;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        window.quit();
      } else if (event.type == SDL_KEYDOWN &&
                 event.key.keysym.sym == SDLK_ESCAPE) {
        mouseLook = !mouseLook;
        SDL_SetRelativeMouseMode(mouseLook ? SDL_TRUE : SDL_FALSE);
      } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_r &&
                 !event.key.repeat) {
        reload_pressed = true;
      } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_g &&
                 !event.key.repeat) {
        show_axes_gizmo = !show_axes_gizmo;
      } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                 event.button.button == SDL_BUTTON_LEFT && mouseLook) {
        shoot_pressed = true;
      } else if (event.type == SDL_MOUSEMOTION && mouseLook) {
        mouseRelX += static_cast<float>(event.motion.xrel);
        mouseRelY += static_cast<float>(event.motion.yrel);
      } else if (event.type == SDL_WINDOWEVENT &&
                 (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                  event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
        window.pixel_size(&width, &height);
        renderer.resize(width, height);
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

    const uint8_t *keys = SDL_GetKeyboardState(nullptr);
    float dx = 0.0f;
    float dy = 0.0f;
    float dz = 0.0f;
    fps_camera_apply_wasd(camera, keys, dt, 5.0f, dx, dy, dz);
    camera.eyeX += dx;
    camera.eyeY += dy;
    camera.eyeZ += dz;

    engine::player_physics_step(camera, player_physics, dt, level, prev_eye_x,
                                prev_eye_z);

    // Footsteps follow actual post-physics horizontal movement so wall
    // collisions (which zero out the move) silence the loop even while WASD is
    // held.
    {
      const float move_dx = camera.eyeX - prev_eye_x;
      const float move_dz = camera.eyeZ - prev_eye_z;
      const float min_speed = 0.5f; // m/s
      const float min_step = min_speed * dt;
      const float moved_sq = move_dx * move_dx + move_dz * move_dz;
      is_walking = moved_sq > (min_step * min_step);
      engine::audio_set_looping(step_sound, is_walking);
    }

    const float aspect =
        height > 0 ? static_cast<float>(width) / static_cast<float>(height)
                   : 1.0f;
    float view[16];
    float proj[16];
    fps_camera_view_proj(camera, aspect, bgfx::getCaps()->homogeneousDepth,
                         view, proj);
    bgfx::setViewTransform(0, view, proj);

    bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(width),
                      static_cast<uint16_t>(height));
    bgfx::setViewRect(1, 0, 0, static_cast<uint16_t>(width),
                      static_cast<uint16_t>(height));
    bgfx::setViewTransform(1, view, proj);
    renderer.begin_frame();

    const bool can_shoot = !is_reloading && bullets_in_clip > 0;
    if (shoot_pressed && can_shoot) {
      engine::audio_play(shot_sound);
      --bullets_in_clip;

      // Build the shot ray from the camera (eye) along its forward direction.
      const float fx = std::cos(camera.pitch) * std::sin(camera.yaw);
      const float fy = std::sin(camera.pitch);
      const float fz = std::cos(camera.pitch) * std::cos(camera.yaw);

      float wall_t = std::numeric_limits<float>::infinity();
      engine::ray_walls_nearest(level.walls, camera.eyeX, camera.eyeY, camera.eyeZ,
                                fx, fy, fz, wall_t);

      int best_target = -1;
      float best_t = wall_t;
      for (size_t i = 0; i < targets.size(); ++i) {
        const Target &tgt = targets[i];
        if (!tgt.alive) {
          continue;
        }
        const float h = tgt.half_extent;
        float t_hit;
        const bool hit = engine::ray_aabb(
            camera.eyeX, camera.eyeY, camera.eyeZ, fx, fy, fz, tgt.pos_x - h,
            tgt.pos_y - h, tgt.pos_z - h, tgt.pos_x + h, tgt.pos_y + h,
            tgt.pos_z + h, t_hit);
        if (hit && t_hit < best_t) {
          best_t = t_hit;
          best_target = static_cast<int>(i);
        }
      }
      if (best_target >= 0) {
        Target &tgt = targets[static_cast<size_t>(best_target)];
        if (--tgt.hits_remaining <= 0) {
          tgt.alive = false;
        }
      }
    } else {
      shoot_pressed = false;
    }

    const bool can_reload = !is_reloading && bullets_in_clip < clip_size;
    if (reload_pressed && can_reload) {
      is_reloading = true;
    } else {
      reload_pressed = false;
    }

    // Animation control: explicit input wins; otherwise loop Idle once
    // one-shots end.
    if (viewmodel.valid()) {
      const engine::ViewmodelAnim cur = viewmodel.current_anim();
      if (is_reloading && cur == engine::ViewmodelAnim::Reload &&
          viewmodel.current_anim_finished()) {
        bullets_in_clip = clip_size;
        is_reloading = false;
      }

      if (shoot_pressed) {
        viewmodel.play(engine::ViewmodelAnim::Shoot, false, true);
      } else if (reload_pressed) {
        viewmodel.play(engine::ViewmodelAnim::Reload, false, true);
      } else {
        const bool one_shot = cur == engine::ViewmodelAnim::Shoot ||
                              cur == engine::ViewmodelAnim::Reload ||
                              cur == engine::ViewmodelAnim::Take;
        if (one_shot && viewmodel.current_anim_finished()) {
          viewmodel.play(engine::ViewmodelAnim::Idle, true, false);
        }
      }
      viewmodel.update(dt);
    }

    bgfx::dbgTextClear();
    bgfx::dbgTextPrintf(
        0, 1, 0x0f, "WASD  Mouse  Esc: %s   G: gizmo=%s   LMB:Shoot R:Reload",
        mouseLook ? "free cursor" : "capture", show_axes_gizmo ? "on" : "off");
    bgfx::dbgTextPrintf(
        0, 2, 0x0f, "level=%s  sectors=%zu walls=%zu stairs=%zu",
        level.name.empty() ? "(unnamed)" : level.name.c_str(),
        level.sectors.size(), level.walls.size(), level.stairs.size());

    std::array<float, static_cast<size_t>(k_max_shader_lights) * 4>
        light_pos_pack{};
    std::array<float, static_cast<size_t>(k_max_shader_lights) * 4>
        light_color_pack{};

    size_t n_lights = level.lights.size();
    if (n_lights == 0) {
      light_pos_pack[0] = 0.0f;
      light_pos_pack[1] = 4.0f;
      light_pos_pack[2] = 0.0f;
      light_pos_pack[3] = 0.0f;
      light_color_pack[0] = 2.4f;
      light_color_pack[1] = 2.1f;
      light_color_pack[2] = 1.7f;
      light_color_pack[3] = 0.0f;
      n_lights = 1;
    } else {
      n_lights = std::min(n_lights, static_cast<size_t>(k_max_shader_lights));
      for (size_t i = 0; i < n_lights; ++i) {
        const engine::Light &L = level.lights[i];
        light_pos_pack[i * 4 + 0] = L.pos.x;
        light_pos_pack[i * 4 + 1] = L.pos.y;
        light_pos_pack[i * 4 + 2] = L.pos.z;
        light_pos_pack[i * 4 + 3] = 0.0f;
        light_color_pack[i * 4 + 0] = L.color[0] * L.intensity;
        light_color_pack[i * 4 + 1] = L.color[1] * L.intensity;
        light_color_pack[i * 4 + 2] = L.color[2] * L.intensity;
        light_color_pack[i * 4 + 3] = 0.0f;
      }
    }

    const float light_params[4] = {static_cast<float>(n_lights), 0.0f, 0.0f,
                                   0.0f};
    const float ambient[4] = {level.ambient[0], level.ambient[1],
                              level.ambient[2], 0.0f};
    bgfx::setUniform(u_light_pos, light_pos_pack.data(), k_max_shader_lights);
    bgfx::setUniform(u_light_color, light_color_pack.data(),
                     k_max_shader_lights);
    bgfx::setUniform(u_light_params, light_params);
    bgfx::setUniform(u_ambient, ambient);

    float model[16];
    const uint64_t renderState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                                 BGFX_STATE_WRITE_Z |
                                 BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA;

    auto submit_pass = [&](bgfx::VertexBufferHandle vbh,
                           bgfx::TextureHandle tex) {
      if (!bgfx::isValid(vbh)) {
        return;
      }
      bgfx::setState(renderState);
      bx::mtxIdentity(model);
      bgfx::setTransform(model);
      bgfx::setTexture(0, s_albedo, tex);
      bgfx::setVertexBuffer(0, vbh);
      bgfx::submit(0, program);
    };
    submit_pass(floor_vbh, floor_bind);
    submit_pass(wall_vbh, wall_bind);
    submit_pass(stair_vbh, wall_bind);

    auto draw_bulb = [&](float lx, float ly, float lz) {
      bgfx::setState(renderState);
      bx::mtxTranslate(model, lx, ly, lz);
      bgfx::setTransform(model);
      bgfx::setTexture(0, s_albedo, white_tex);
      bgfx::setVertexBuffer(0, bulb_vbh);
      bgfx::setIndexBuffer(bulb_ibh);
      bgfx::submit(0, program);
    };
    if (level.lights.empty()) {
      draw_bulb(0.0f, 4.0f, 0.0f);
    } else {
      for (const engine::Light &L : level.lights) {
        draw_bulb(L.pos.x, L.pos.y, L.pos.z);
      }
    }

    // Targets — bind a different VBH per damage state so the cube visibly
    // darkens.
    for (const Target &tgt : targets) {
      if (!tgt.alive) {
        continue;
      }
      const int tint = k_target_max_hits - tgt.hits_remaining;
      const int tint_idx = std::clamp(tint, 0, k_target_max_hits - 1);
      bgfx::setState(renderState);
      bx::mtxTranslate(model, tgt.pos_x, tgt.pos_y, tgt.pos_z);
      bgfx::setTransform(model);
      bgfx::setTexture(0, s_albedo, white_tex);
      bgfx::setVertexBuffer(0, cube_vbh[tint_idx]);
      bgfx::setIndexBuffer(cube_ibh);
      bgfx::submit(0, program);
    }

    if (viewmodel.valid() && bgfx::isValid(skinned_program)) {
      engine::ViewmodelDrawParams vmp{};
      vmp.eye[0] = camera.eyeX;
      vmp.eye[1] = camera.eyeY;
      vmp.eye[2] = camera.eyeZ;
      vmp.yaw = -camera.yaw;
      vmp.pitch = -camera.pitch;
      vmp.offset[0] = 0.15f;   // right
      vmp.offset[1] = -1.575f; // down
      vmp.offset[2] = 0.0f;    // forward
      vmp.tweak_pitch = 0.0f;
      // vmp.tweak_yaw = bx::kPi;  // model's local -Z front → flip to face away
      // from camera
      vmp.tweak_yaw =
          0.0f; // model's local -Z front → flip to face away from camera
      vmp.tweak_roll = 0.0f;
      vmp.scale = 1.0f;
      viewmodel.submit(1, skinned_program, u_bones, s_albedo, u_baseColor,
                       white_tex, renderState, vmp);
      if (show_axes_gizmo && bgfx::isValid(debug_program)) {
        viewmodel.submit_axes_gizmo(1, debug_program, vmp, 0.30f);
      }
    }

    if (hud_ok) {
      constexpr bgfx::ViewId k_hud_view = 2;
      engine::hud_begin_frame(k_hud_view, width, height);

      constexpr uint32_t k_hud_red = 0xff2030e0; // ABGR — red with full alpha
      const float margin_x = 28.0f;
      const float margin_bottom = 24.0f;
      // hud_descent() is negative (stb metric); subtracting lifts the baseline
      // up so descenders sit just above margin_bottom from the screen edge.
      const float baseline_y =
          static_cast<float>(height) - margin_bottom + engine::hud_descent();

      char life_buf[16];
      std::snprintf(life_buf, sizeof(life_buf), "%d%%", player_health);
      engine::hud_draw_text(life_buf, margin_x, baseline_y, k_hud_red);

      char ammo_buf[16];
      std::snprintf(ammo_buf, sizeof(ammo_buf), "%d/%d", bullets_in_clip,
                    clip_size);
      engine::hud_draw_text_right(ammo_buf,
                                  static_cast<float>(width) - margin_x,
                                  baseline_y, k_hud_red);

      // Minimal white crosshair: a 3px square dot at the screen center.
      constexpr uint32_t k_crosshair_white = 0xffffffffu;
      const float dot_size = 3.0f;
      const float cx = (static_cast<float>(width) - dot_size) * 0.5f;
      const float cy = (static_cast<float>(height) - dot_size) * 0.5f;
      engine::hud_draw_solid_rect(cx, cy, dot_size, dot_size,
                                  k_crosshair_white);
    }

    renderer.end_frame();
  }

  if (mouseLook) {
    SDL_SetRelativeMouseMode(SDL_FALSE);
  }

  engine::audio_shutdown();
  engine::hud_shutdown();
  viewmodel.unload();
  if (bgfx::isValid(floor_tex)) {
    bgfx::destroy(floor_tex);
  }
  if (bgfx::isValid(wall_tex)) {
    bgfx::destroy(wall_tex);
  }
  bgfx::destroy(white_tex);
  bgfx::destroy(s_albedo);
  bgfx::destroy(u_baseColor);
  bgfx::destroy(u_bones);
  bgfx::destroy(u_ambient);
  bgfx::destroy(u_light_params);
  bgfx::destroy(u_light_color);
  bgfx::destroy(u_light_pos);
  if (bgfx::isValid(hud_program))
    bgfx::destroy(hud_program);
  if (bgfx::isValid(debug_program))
    bgfx::destroy(debug_program);
  if (bgfx::isValid(skinned_program))
    bgfx::destroy(skinned_program);
  bgfx::destroy(program);
  if (bgfx::isValid(cube_ibh))
    bgfx::destroy(cube_ibh);
  for (int t = 0; t < k_target_max_hits; ++t) {
    if (bgfx::isValid(cube_vbh[t]))
      bgfx::destroy(cube_vbh[t]);
  }
  bgfx::destroy(bulb_ibh);
  bgfx::destroy(bulb_vbh);
  if (bgfx::isValid(stair_vbh))
    bgfx::destroy(stair_vbh);
  if (bgfx::isValid(wall_vbh))
    bgfx::destroy(wall_vbh);
  if (bgfx::isValid(floor_vbh))
    bgfx::destroy(floor_vbh);

  return 0;
}
