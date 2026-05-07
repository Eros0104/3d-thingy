#include "game/game_state.hpp"

#include "engine/audio.hpp"
#include "engine/geometry/primitives.hpp"
#include "engine/hud.hpp"
#include "engine/lit_vertex.hpp"
#include "engine/log.hpp"
#include "engine/physics/jolt_physics.hpp"
#include "engine/physics/raycast.hpp"
#include "engine/render/buffers.hpp"
#include "engine/shader_program.hpp"
#include "engine/texture_loader.hpp"
#include "game/level/level_loader.hpp"
#include "game/level/level_mesh.hpp"

// Embedded textures
#include <bullet_hole_tex.h>
#include <wall_albedo.h>
#include <woodfloor_color.h>
#include <woodfloor_normal.h>
#include <woodfloor_roughness.h>

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

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

// ============================================================
// Construction / destruction
// ============================================================

GameState::GameState() = default;
GameState::~GameState() = default;

// ============================================================
// init
// ============================================================

bool GameState::init(const char *level_path, int width, int height) {
  width_ = width;
  height_ = height;

  // Audio
  engine::SoundId shot_sound = engine::k_invalid_sound;
  engine::SoundId step_sound = engine::k_invalid_sound;
  {
    std::string err;
    if (!engine::audio_init(err)) {
      LOG_WARN("audio: %s (continuing without sound)", err.c_str());
    } else {
      shot_sound =
          engine::audio_load(ENGINE_SOUNDS_DIR "/pistol_shot.mp3", err);
      if (shot_sound == engine::k_invalid_sound)
        LOG_WARN("audio: %s", err.c_str());
      step_sound = engine::audio_load(ENGINE_SOUNDS_DIR "/footsteps.mp3", err);
      if (step_sound == engine::k_invalid_sound)
        LOG_WARN("audio: %s", err.c_str());
    }
  }

  // Level
  {
    std::string err;
    if (!engine::load_level_any(level_path, level_, err)) {
      LOG_ERROR("load_level: %s", err.c_str());
      return false;
    }
  }

  // Physics
  {
    std::string err;
    if (!jolt_.init()) {
      LOG_ERROR("Jolt physics init failed");
      return false;
    }
  }

  // Level geometry + physics mesh
  {
    engine::LevelMeshOutput meshes;
    engine::build_level_meshes(level_, meshes);
    if (meshes.floor_vertices.empty()) {
      LOG_ERROR("level has no floor geometry (sectors empty?)");
      return false;
    }

    jolt_.build_level(meshes);

    layout_.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    floor_vbh_ = engine::create_vertex_buffer(meshes.floor_vertices, layout_);
    wall_vbh_ = engine::create_vertex_buffer(meshes.wall_vertices, layout_);
    stair_vbh_ = engine::create_vertex_buffer(meshes.stair_vertices, layout_);
  }

  // Shaders
  program_ = engine::load_triangle_program();
  skinned_program_ = engine::load_skinned_program();
  debug_program_ = engine::load_debug_program();
  hud_program_ = engine::load_hud_program();

  if (!bgfx::isValid(program_)) {
    LOG_ERROR("triangle shader failed");
    return false;
  }

  // HUD
  if (bgfx::isValid(hud_program_)) {
    engine::HudInitDesc desc;
    desc.font_path = ENGINE_FONTS_DIR "/moms_typewriter/moms_typewriter.ttf";
    desc.pixel_height = 36.0f;
    desc.program = hud_program_;
    std::string err;
    if (!engine::hud_init(desc, err))
      LOG_WARN("hud: %s (continuing without HUD)", err.c_str());
    else
      hud_ok_ = true;
  }

  // Uniforms
  u_light_pos_ = bgfx::createUniform("u_lightPos", bgfx::UniformType::Vec4,
                                     k_max_shader_lights);
  u_light_color_ = bgfx::createUniform("u_lightColor", bgfx::UniformType::Vec4,
                                       k_max_shader_lights);
  u_light_params_ =
      bgfx::createUniform("u_lightParams", bgfx::UniformType::Vec4);
  u_ambient_ = bgfx::createUniform("u_ambient", bgfx::UniformType::Vec4);
  u_cam_pos_ = bgfx::createUniform("u_cameraPos", bgfx::UniformType::Vec4);
  u_wall_segs_ = bgfx::createUniform("u_wallSegs", bgfx::UniformType::Vec4,
                                     k_max_shader_walls);
  u_wall_params_ = bgfx::createUniform("u_wallParams", bgfx::UniformType::Vec4);
  s_albedo_ = bgfx::createUniform("s_albedo", bgfx::UniformType::Sampler);
  s_normal_ = bgfx::createUniform("s_normal", bgfx::UniformType::Sampler);
  s_roughness_ = bgfx::createUniform("s_roughness", bgfx::UniformType::Sampler);
  u_bones_ = bgfx::createUniform("u_bones", bgfx::UniformType::Mat4, 120);
  u_baseColor_ = bgfx::createUniform("u_baseColor", bgfx::UniformType::Vec4);

  // Textures
  static constexpr uint32_t k_white = 0xffffffffu;
  white_tex_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                                     0, bgfx::copy(&k_white, sizeof(k_white)));

  // 1×1 flat normal map: (128,128,255,255) = tangent-space (0,0,1)
  static constexpr uint32_t k_flat_normal =
      0xffff8080u; // ABGR: a=ff,b=ff,g=80,r=80
  flat_normal_tex_ =
      bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0,
                            bgfx::copy(&k_flat_normal, sizeof(k_flat_normal)));

  // Floor material: woodfloor (embedded)
  floor_mat_.albedo = engine::load_texture_from_memory(
      woodfloor_color, sizeof(woodfloor_color), "woodfloor_color");
  floor_mat_.normal = engine::load_texture_from_memory(
      woodfloor_normal, sizeof(woodfloor_normal), "woodfloor_normal");
  floor_mat_.roughness = engine::load_texture_from_memory(
      woodfloor_roughness, sizeof(woodfloor_roughness), "woodfloor_roughness");

  // Wall material: plaster albedo only, flat normal, full roughness (embedded)
  wall_mat_.albedo = engine::load_texture_from_memory(
      wall_albedo, sizeof(wall_albedo), "wall_albedo");
  wall_mat_.normal = flat_normal_tex_;
  wall_mat_.roughness = white_tex_;

  bullet_hole_tex_ = engine::load_texture_from_memory(
      bullet_hole_tex, sizeof(bullet_hole_tex), "bullet_hole");

  // Light bulb sphere
  {
    std::vector<LitVertex> verts;
    std::vector<uint16_t> indices;
    engine::build_uv_sphere(verts, indices, 0.14f, 10, 14, 0xffffffffu);
    bulb_vbh_ = bgfx::createVertexBuffer(
        bgfx::copy(verts.data(), uint32_t(verts.size() * sizeof(LitVertex))),
        layout_);
    bulb_ibh_ = bgfx::createIndexBuffer(bgfx::copy(
        indices.data(), uint32_t(indices.size() * sizeof(uint16_t))));
  }

  render_state_ = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                  BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS |
                  BGFX_STATE_CULL_CCW | BGFX_STATE_MSAA;

  // Player
  {
    std::string err;
    engine::RiggedModel *vm =
        load_model(ENGINE_MODELS_DIR "/animated_pistol-v2.glb", err);
    if (!vm)
      LOG_WARN("viewmodel: %s", err.c_str());
    const float spawn_yaw = level_.spawn.yaw_deg * (bx::kPi / 180.0f);
    const float spawn_y =
        level_.spawn.pos.y + game::Player::k_eye_height + 0.02f;
    player_.init(level_.spawn.pos.x, spawn_y, level_.spawn.pos.z, spawn_yaw,
                 jolt_, vm, shot_sound, step_sound);
  }

  if (SDL_SetRelativeMouseMode(SDL_TRUE) != 0)
    LOG_WARN("SDL_SetRelativeMouseMode: %s", SDL_GetError());

  // Spawn initial entities
  spawn_zombie(10.0f, 0.0f, 20.0f);
  spawn_zombie(4.0f, 0.0f, 7.0f);
  for (auto &z : zombies_)
    z.init_jolt(jolt_);

  return true;
}

// ============================================================
// shutdown
// ============================================================

void GameState::shutdown() {
  if (player_.mouse_look())
    SDL_SetRelativeMouseMode(SDL_FALSE);

  engine::audio_shutdown();
  engine::hud_shutdown();
  jolt_.shutdown();

  for (auto &m : models_)
    m->unload();
  models_.clear();

  auto dvb = [](bgfx::VertexBufferHandle h) {
    if (bgfx::isValid(h))
      bgfx::destroy(h);
  };
  auto dib = [](bgfx::IndexBufferHandle h) {
    if (bgfx::isValid(h))
      bgfx::destroy(h);
  };
  auto dph = [](bgfx::ProgramHandle h) {
    if (bgfx::isValid(h))
      bgfx::destroy(h);
  };
  auto dth = [](bgfx::TextureHandle h) {
    if (bgfx::isValid(h))
      bgfx::destroy(h);
  };
  auto duh = [](bgfx::UniformHandle h) {
    if (bgfx::isValid(h))
      bgfx::destroy(h);
  };

  dvb(floor_vbh_);
  dvb(wall_vbh_);
  dvb(stair_vbh_);
  dvb(bulb_vbh_);
  dib(bulb_ibh_);
  dib(cube_ibh_);

  dth(white_tex_);
  dth(flat_normal_tex_);
  dth(bullet_hole_tex_);
  dth(floor_mat_.albedo);
  dth(floor_mat_.normal);
  dth(floor_mat_.roughness);
  dth(wall_mat_.albedo);
  // wall_mat_.normal == flat_normal_tex_ (already destroyed above)
  // wall_mat_.roughness == white_tex_ (already destroyed above)

  duh(u_light_pos_);
  duh(u_light_color_);
  duh(u_light_params_);
  duh(u_ambient_);
  duh(u_cam_pos_);
  duh(u_wall_segs_);
  duh(u_wall_params_);
  duh(s_albedo_);
  duh(s_normal_);
  duh(s_roughness_);
  duh(u_bones_);
  duh(u_baseColor_);

  dph(program_);
  dph(skinned_program_);
  dph(debug_program_);
  dph(hud_program_);
}

// ============================================================
// Entity factories
// ============================================================

engine::RiggedModel *GameState::load_model(const char *path, std::string &err) {
  auto m = std::make_unique<engine::RiggedModel>();
  if (!m->load(path, err))
    return nullptr;
  models_.push_back(std::move(m));
  return models_.back().get();
}

void GameState::spawn_zombie(float x, float y, float z) {
  std::string err;
  engine::RiggedModel *m =
      load_model(ENGINE_MODELS_DIR "/ZombieCity01_Shirt.glb", err);
  if (!m)
    LOG_WARN("zombie model: %s", err.c_str());
  zombies_.emplace_back(x, y, z, m);
}

// ============================================================
// Event handling
// ============================================================

void GameState::on_resize(int width, int height) {
  width_ = width;
  height_ = height;
}

void GameState::handle_event(const SDL_Event &event) {
  player_.handle_event(event);
  if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_c &&
      !event.key.repeat)
    show_collision_ = !show_collision_;
  if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_l &&
      !event.key.repeat)
    show_lights_ = !show_lights_;
}

// ============================================================
// Update
// ============================================================

void GameState::update(float dt) {
  jolt_.update(dt);
  player_.update(dt, jolt_);
  sys_shooting();
  sys_zombie_ai(dt);
  particles_.update(dt);
}

// --- system: shoot ---

void GameState::sys_shooting() {
  float fx, fy, fz;
  if (!player_.try_fire(fx, fy, fz))
    return;

  const FpsCamera &cam = player_.camera();
  constexpr float k_max_ray = 500.f;

  // Level geometry raycast via Jolt.
  engine::JoltRayHit jolt_hit;
  const bool wall_hit = jolt_.cast_ray_level(cam.eyeX, cam.eyeY, cam.eyeZ, fx,
                                             fy, fz, k_max_ray, jolt_hit);
  const float wall_t = jolt_hit.t;

  float best_t = wall_hit ? wall_t : std::numeric_limits<float>::infinity();

  // Zombies: capsule test, only if closer than wall/target.
  for (auto &z : zombies_) {
    if (!z.alive())
      continue;
    float ca[3], cb[3];
    z.capsule(ca, cb);
    float t_hit;
    if (engine::ray_capsule(cam.eyeX, cam.eyeY, cam.eyeZ, fx, fy, fz, ca[0],
                            ca[1], ca[2], cb[0], cb[1], cb[2],
                            game::Zombie::k_collider.radius, t_hit) &&
        t_hit < best_t) {
      best_t = t_hit;
      z.take_damage(10);
      particles_.spawn(cam.eyeX + fx * t_hit, cam.eyeY + fy * t_hit,
                       cam.eyeZ + fz * t_hit, 25);
    }
  }

  // Bullet hole on wall if level was the closest hit.
  if (wall_hit && best_t >= wall_t - 1e-4f) {
    constexpr float k_offset = 0.005f; // small offset to avoid z-fighting
    constexpr int k_max = 200;
    bullet_holes_.push_back({cam.eyeX + fx * wall_t + jolt_hit.nx * k_offset,
                             cam.eyeY + fy * wall_t + jolt_hit.ny * k_offset,
                             cam.eyeZ + fz * wall_t + jolt_hit.nz * k_offset,
                             jolt_hit.nx, jolt_hit.ny, jolt_hit.nz});
    if (int(bullet_holes_.size()) > k_max)
      bullet_holes_.erase(bullet_holes_.begin());
  }
}

// --- system: zombie AI ---

void GameState::sys_zombie_ai(float dt) {
  for (auto &z : zombies_)
    z.update(dt, player_, jolt_);
}

// ============================================================
// Render
// ============================================================

void GameState::render(int width, int height) {
  width_ = width;
  height_ = height;

  const float aspect = height > 0 ? float(width) / float(height) : 1.f;
  float view[16], proj[16];
  fps_camera_view_proj(player_.camera(), aspect,
                       bgfx::getCaps()->homogeneousDepth, view, proj);

  bgfx::setViewTransform(0, view, proj);
  bgfx::setViewRect(0, 0, 0, uint16_t(width), uint16_t(height));
  bgfx::setViewRect(1, 0, 0, uint16_t(width), uint16_t(height));
  bgfx::setViewTransform(1, view, proj);

  sys_set_lights();

  // Camera world position for specular calculation
  const FpsCamera &cam = player_.camera();
  const float cam_pos[4] = {cam.eyeX, cam.eyeY, cam.eyeZ, 0.f};
  bgfx::setUniform(u_cam_pos_, cam_pos);

  sys_render_level();
  sys_render_bullet_holes();
  sys_render_characters();
  particles_.render(0, debug_program_);
  if (show_collision_)
    sys_render_collision_debug();
  if (show_lights_)
    sys_render_debug_lights();
  sys_render_hud();
  sys_render_debug_text();
}

void GameState::sys_set_lights() {
  std::array<float, k_max_shader_lights * 4> pos_pack{};
  std::array<float, k_max_shader_lights * 4> col_pack{};

  size_t n = level_.lights.size();
  if (n == 0) {
    pos_pack[0] = 0.f;
    pos_pack[1] = 4.f;
    pos_pack[2] = 0.f;
    pos_pack[3] = 10.f;
    col_pack[0] = 2.4f;
    col_pack[1] = 2.1f;
    col_pack[2] = 1.7f;
    col_pack[3] = 0.f;
    n = 1;
  } else {
    n = std::min(n, size_t(k_max_shader_lights));
    for (size_t i = 0; i < n; ++i) {
      const engine::Light &L = level_.lights[i];
      pos_pack[i * 4 + 0] = L.pos.x;
      pos_pack[i * 4 + 1] = L.pos.y;
      pos_pack[i * 4 + 2] = L.pos.z;
      pos_pack[i * 4 + 3] = L.radius;
      col_pack[i * 4 + 0] = L.color[0] * L.intensity;
      col_pack[i * 4 + 1] = L.color[1] * L.intensity;
      col_pack[i * 4 + 2] = L.color[2] * L.intensity;
    }
  }

  const float params[4] = {float(n), 0.f, 0.f, 0.f};
  const float amb[4] = {level_.ambient[0], level_.ambient[1], level_.ambient[2],
                        0.f};
  bgfx::setUniform(u_light_pos_, pos_pack.data(), k_max_shader_lights);
  bgfx::setUniform(u_light_color_, col_pack.data(), k_max_shader_lights);
  bgfx::setUniform(u_light_params_, params);
  bgfx::setUniform(u_ambient_, amb);

  // Pack solid wall sub-segments for 2D occlusion, splitting out brush openings
  // so doors and windows allow light to pass through.
  std::array<float, k_max_shader_walls * 4> wall_pack{};
  size_t nw = 0;

  for (size_t wi = 0; wi < level_.walls.size(); ++wi) {
    if (nw >= size_t(k_max_shader_walls))
      break;
    const engine::Wall &W = level_.walls[wi];
    const float dx = W.b.x - W.a.x;
    const float dz = W.b.z - W.a.z;
    const float len = std::sqrt(dx * dx + dz * dz);
    if (len <= 1e-6f)
      continue;

    // Collect horizontal brush openings on this wall as parametric [t0, t1]
    // spans.
    struct Span {
      float t0, t1;
    };
    std::vector<Span> openings;
    for (const auto &B : level_.brushes) {
      if (B.wall != int(wi))
        continue;
      const float t0 = std::max(0.f, B.offset / len);
      const float t1 = std::min(1.f, (B.offset + B.width) / len);
      if (t1 > t0 + 1e-6f)
        openings.push_back({t0, t1});
    }
    std::sort(openings.begin(), openings.end(),
              [](const Span &a, const Span &b) { return a.t0 < b.t0; });

    // Emit the solid segments that remain between (and around) openings.
    auto emit = [&](float t0, float t1) {
      if (nw >= size_t(k_max_shader_walls))
        return;
      if (t1 <= t0 + 1e-6f)
        return;
      wall_pack[nw * 4 + 0] = W.a.x + dx * t0;
      wall_pack[nw * 4 + 1] = W.a.z + dz * t0;
      wall_pack[nw * 4 + 2] = W.a.x + dx * t1;
      wall_pack[nw * 4 + 3] = W.a.z + dz * t1;
      ++nw;
    };

    float cursor = 0.f;
    for (const auto &sp : openings) {
      emit(cursor, sp.t0);
      cursor = sp.t1;
    }
    emit(cursor, 1.f);
  }

  const float wparams[4] = {float(nw), 0.f, 0.f, 0.f};
  bgfx::setUniform(u_wall_segs_, wall_pack.data(), k_max_shader_walls);
  bgfx::setUniform(u_wall_params_, wparams);
}

void GameState::sys_render_level() {
  float mtx[16];

  auto submit_geo = [&](bgfx::VertexBufferHandle vbh,
                        const engine::Material &mat) {
    if (!bgfx::isValid(vbh))
      return;
    bgfx::setState(render_state_);
    bx::mtxIdentity(mtx);
    bgfx::setTransform(mtx);
    bgfx::setTexture(0, s_albedo_,
                     bgfx::isValid(mat.albedo) ? mat.albedo : white_tex_);
    bgfx::setTexture(1, s_normal_,
                     bgfx::isValid(mat.normal) ? mat.normal : flat_normal_tex_);
    bgfx::setTexture(2, s_roughness_,
                     bgfx::isValid(mat.roughness) ? mat.roughness : white_tex_);
    bgfx::setVertexBuffer(0, vbh);
    bgfx::submit(0, program_);
  };

  submit_geo(floor_vbh_, floor_mat_);
  submit_geo(wall_vbh_, wall_mat_);
  submit_geo(stair_vbh_, wall_mat_);

  auto draw_bulb = [&](float lx, float ly, float lz) {
    bgfx::setState(render_state_);
    bx::mtxTranslate(mtx, lx, ly, lz);
    bgfx::setTransform(mtx);
    bgfx::setTexture(0, s_albedo_, white_tex_);
    bgfx::setTexture(1, s_normal_, flat_normal_tex_);
    bgfx::setTexture(2, s_roughness_, white_tex_);
    bgfx::setVertexBuffer(0, bulb_vbh_);
    bgfx::setIndexBuffer(bulb_ibh_);
    bgfx::submit(0, program_);
  };
  if (level_.lights.empty()) {
    draw_bulb(0.f, 4.f, 0.f);
  } else {
    for (const auto &L : level_.lights)
      draw_bulb(L.pos.x, L.pos.y, L.pos.z);
  }
}

void GameState::sys_render_characters() {
  if (!bgfx::isValid(skinned_program_))
    return;

  // Player viewmodel
  player_.render_viewmodel(1, skinned_program_, u_bones_, s_albedo_,
                           u_baseColor_, white_tex_, render_state_,
                           debug_program_);

  // World-space characters (zombies)
  for (const auto &z : zombies_)
    z.render(0, skinned_program_, u_bones_, s_albedo_, u_baseColor_, white_tex_,
             render_state_);
}

void GameState::sys_render_bullet_holes() {
  if (bullet_holes_.empty() || !bgfx::isValid(program_))
    return;

  const uint32_t n = uint32_t(bullet_holes_.size());
  const uint32_t n_verts = n * 4;
  const uint32_t n_indices = n * 6;
  if (bgfx::getAvailTransientVertexBuffer(n_verts, layout_) < n_verts)
    return;
  if (bgfx::getAvailTransientIndexBuffer(n_indices) < n_indices)
    return;

  bgfx::TransientVertexBuffer tvb;
  bgfx::TransientIndexBuffer tib;
  bgfx::allocTransientVertexBuffer(&tvb, n_verts, layout_);
  bgfx::allocTransientIndexBuffer(&tib, n_indices);

  LitVertex *vp = reinterpret_cast<LitVertex *>(tvb.data);
  uint16_t *ip = reinterpret_cast<uint16_t *>(tib.data);

  constexpr float k_hs = 0.08f; // half-size in metres

  for (uint32_t i = 0; i < n; ++i) {
    const BulletHole &h = bullet_holes_[i];

    // Build a tangent frame on the hit surface.
    // right = cross(normal, up_ref); up_in_plane = cross(right, normal)
    float rx, ry, rz, upx, upy, upz;
    if (std::fabs(h.ny) < 0.9f) {
      // Wall: world Y is the up reference. right = cross(N, worldY) = (nz, 0,
      // -nx).
      const float rlen = std::sqrt(h.nx * h.nx + h.nz * h.nz);
      rx = (rlen > 1e-6f) ? h.nz / rlen : 1.f;
      ry = 0.f;
      rz = (rlen > 1e-6f) ? -h.nx / rlen : 0.f;
      upx = 0.f;
      upy = 1.f;
      upz = 0.f;
    } else {
      // Floor/ceiling: world X as right, compute up in plane.
      rx = 1.f;
      ry = 0.f;
      rz = 0.f;
      // up = cross(right, normal) = (ry*nz - rz*ny, rz*nx - rx*nz, rx*ny -
      // ry*nx)
      upx = -h.nz;
      upy = 0.f;
      upz = h.nx; // simplified for rx=1
      const float ulen = std::sqrt(upx * upx + upz * upz);
      if (ulen > 1e-6f) {
        upx /= ulen;
        upz /= ulen;
      }
    }

    auto vert = [&](float px, float py, float pz, float u,
                    float v) -> LitVertex {
      return {px, py, pz, h.nx, h.ny, h.nz, u, v, 0xffffffff};
    };
    vp[i * 4 + 0] = vert(h.x + (-rx + upx) * k_hs, h.y + (-ry + upy) * k_hs,
                         h.z + (-rz + upz) * k_hs, 0.f, 0.f);
    vp[i * 4 + 1] = vert(h.x + (rx + upx) * k_hs, h.y + (ry + upy) * k_hs,
                         h.z + (rz + upz) * k_hs, 1.f, 0.f);
    vp[i * 4 + 2] = vert(h.x + (rx - upx) * k_hs, h.y + (ry - upy) * k_hs,
                         h.z + (rz - upz) * k_hs, 1.f, 1.f);
    vp[i * 4 + 3] = vert(h.x + (-rx - upx) * k_hs, h.y + (-ry - upy) * k_hs,
                         h.z + (-rz - upz) * k_hs, 0.f, 1.f);

    const uint16_t b = uint16_t(i * 4);
    ip[i * 6 + 0] = b + 0;
    ip[i * 6 + 1] = b + 1;
    ip[i * 6 + 2] = b + 2;
    ip[i * 6 + 3] = b + 0;
    ip[i * 6 + 4] = b + 2;
    ip[i * 6 + 5] = b + 3;
  }

  const bgfx::TextureHandle tex =
      bgfx::isValid(bullet_hole_tex_) ? bullet_hole_tex_ : white_tex_;

  float mtx[16];
  bx::mtxIdentity(mtx);
  const uint64_t blend = BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                               BGFX_STATE_BLEND_INV_SRC_ALPHA);
  bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                 BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA | blend);
  bgfx::setTransform(mtx);
  bgfx::setTexture(0, s_albedo_, tex);
  bgfx::setTexture(1, s_normal_, flat_normal_tex_);
  bgfx::setTexture(2, s_roughness_, white_tex_);
  bgfx::setVertexBuffer(0, &tvb);
  bgfx::setIndexBuffer(&tib);
  bgfx::submit(0, program_);
}

namespace {

void draw_capsule_wireframe(bgfx::ViewId view_id, bgfx::ProgramHandle prog,
                            float cx, float feet_y, float cz, float radius,
                            float height, uint32_t abgr) {
  if (!bgfx::isValid(prog))
    return;

  // N segments per full circle; N/2 per hemisphere arc.
  // Layout: 2 equatorial circles + 4 vertical shaft lines + 4 hemisphere arcs
  // (2 per end).
  constexpr int N = 16;
  constexpr int n_verts = N * 2 * 2 + 4 * 2 + 4 * (N / 2) * 2;

  bgfx::VertexLayout layout;
  layout.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .end();

  if (bgfx::getAvailTransientVertexBuffer(n_verts, layout) < n_verts)
    return;

  bgfx::TransientVertexBuffer tvb;
  bgfx::allocTransientVertexBuffer(&tvb, n_verts, layout);

  struct V {
    float x, y, z;
    uint32_t abgr;
  };
  V *v = reinterpret_cast<V *>(tvb.data);
  int idx = 0;

  // Hemisphere sphere-centers: not the pole tips, but the equatorial ring
  // positions.
  const float bottom_y = feet_y + radius;       // center of bottom hemisphere
  const float top_y = feet_y + height - radius; // center of top hemisphere
  constexpr float k_2pi = 6.28318530717958647692f;
  constexpr float k_pi = 3.14159265358979323846f;

  // Equatorial circles at hemisphere centers (the widest cross-section of each
  // cap).
  auto circle = [&](float ring_y) {
    for (int i = 0; i < N; ++i) {
      const float a0 = float(i) / N * k_2pi;
      const float a1 = float(i + 1) / N * k_2pi;
      v[idx++] = {cx + radius * std::cos(a0), ring_y,
                  cz + radius * std::sin(a0), abgr};
      v[idx++] = {cx + radius * std::cos(a1), ring_y,
                  cz + radius * std::sin(a1), abgr};
    }
  };
  circle(bottom_y);
  circle(top_y);

  // Four vertical shaft lines connecting the two equatorial rings.
  const float offsets[4][2] = {
      {radius, 0}, {-radius, 0}, {0, radius}, {0, -radius}};
  for (auto &o : offsets) {
    v[idx++] = {cx + o[0], bottom_y, cz + o[1], abgr};
    v[idx++] = {cx + o[0], top_y, cz + o[1], abgr};
  }

  // Hemisphere arcs in two perpendicular planes (XY and ZY) at each end.
  // Bottom: sweeps θ from 0 → -π (downward from equatorial ring to pole).
  // Top:    sweeps θ from 0 → +π (upward from equatorial ring to pole).
  auto hemi_arc = [&](float center_y, float theta_start, float theta_end,
                      bool z_axis) {
    for (int i = 0; i < N / 2; ++i) {
      const float a0 =
          theta_start + float(i) / float(N / 2) * (theta_end - theta_start);
      const float a1 =
          theta_start + float(i + 1) / float(N / 2) * (theta_end - theta_start);
      const float y0 = center_y + radius * std::sin(a0);
      const float y1 = center_y + radius * std::sin(a1);
      if (!z_axis) {
        v[idx++] = {cx + radius * std::cos(a0), y0, cz, abgr};
        v[idx++] = {cx + radius * std::cos(a1), y1, cz, abgr};
      } else {
        v[idx++] = {cx, y0, cz + radius * std::cos(a0), abgr};
        v[idx++] = {cx, y1, cz + radius * std::cos(a1), abgr};
      }
    }
  };
  hemi_arc(bottom_y, 0.f, -k_pi, false);
  hemi_arc(bottom_y, 0.f, -k_pi, true);
  hemi_arc(top_y, 0.f, k_pi, false);
  hemi_arc(top_y, 0.f, k_pi, true);

  float mtx[16];
  bx::mtxIdentity(mtx);
  bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                 BGFX_STATE_PT_LINES | BGFX_STATE_DEPTH_TEST_LESS);
  bgfx::setTransform(mtx);
  bgfx::setVertexBuffer(0, &tvb);
  bgfx::submit(view_id, prog);
}

} // namespace

void GameState::sys_render_collision_debug() {
  if (!bgfx::isValid(debug_program_))
    return;

  const FpsCamera &cam = player_.camera();
  draw_capsule_wireframe(0, debug_program_, cam.eyeX,
                         cam.eyeY - game::Player::k_eye_height, cam.eyeZ,
                         game::Player::k_radius, game::Player::k_eye_height,
                         0xff44ff44u);

  for (const auto &z : zombies_) {
    const engine::Collider &zc = z.collider();
    draw_capsule_wireframe(0, debug_program_, z.x, z.y, z.z, zc.radius,
                           zc.height, 0xff4444ffu);
  }
}

void GameState::sys_render_debug_lights() {
  if (!bgfx::isValid(debug_program_))
    return;

  constexpr int N = 32;
  // 3 circles × N line segments × 2 verts each
  constexpr int n_verts = 3 * N * 2;

  bgfx::VertexLayout layout;
  layout.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .end();

  constexpr float k_2pi = 6.28318530717958647692f;

  for (const engine::Light &L : level_.lights) {
    if (bgfx::getAvailTransientVertexBuffer(n_verts, layout) < n_verts)
      break;

    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, n_verts, layout);

    struct V {
      float x, y, z;
      uint32_t abgr;
    };
    V *v = reinterpret_cast<V *>(tvb.data);
    int idx = 0;
    const float cx = L.pos.x, cy = L.pos.y, cz = L.pos.z, r = L.radius;
    const uint32_t col = 0xff00ffffu; // yellow

    // XZ circle (horizontal)
    for (int i = 0; i < N; ++i) {
      const float a0 = float(i) / N * k_2pi;
      const float a1 = float(i + 1) / N * k_2pi;
      v[idx++] = {cx + r * std::cos(a0), cy, cz + r * std::sin(a0), col};
      v[idx++] = {cx + r * std::cos(a1), cy, cz + r * std::sin(a1), col};
    }
    // XY circle (vertical, facing Z)
    for (int i = 0; i < N; ++i) {
      const float a0 = float(i) / N * k_2pi;
      const float a1 = float(i + 1) / N * k_2pi;
      v[idx++] = {cx + r * std::cos(a0), cy + r * std::sin(a0), cz, col};
      v[idx++] = {cx + r * std::cos(a1), cy + r * std::sin(a1), cz, col};
    }
    // ZY circle (vertical, facing X)
    for (int i = 0; i < N; ++i) {
      const float a0 = float(i) / N * k_2pi;
      const float a1 = float(i + 1) / N * k_2pi;
      v[idx++] = {cx, cy + r * std::sin(a0), cz + r * std::cos(a0), col};
      v[idx++] = {cx, cy + r * std::sin(a1), cz + r * std::cos(a1), col};
    }

    float mtx[16];
    bx::mtxIdentity(mtx);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                   BGFX_STATE_PT_LINES | BGFX_STATE_DEPTH_TEST_LESS);
    bgfx::setTransform(mtx);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::submit(0, debug_program_);
  }
}

void GameState::sys_render_hud() {
  if (!hud_ok_)
    return;

  constexpr bgfx::ViewId k_hud_view = 2;
  engine::hud_begin_frame(k_hud_view, width_, height_);

  constexpr uint32_t k_hud_red = 0xff2030e0u;
  const float margin_x = 28.f;
  const float margin_bottom = 24.f;
  const float baseline_y =
      float(height_) - margin_bottom + engine::hud_descent();

  char buf[16];
  std::snprintf(buf, sizeof(buf), "%d%%", player_.health());
  engine::hud_draw_text(buf, margin_x, baseline_y, k_hud_red);

  std::snprintf(buf, sizeof(buf), "%d/%d", player_.bullets_in_clip(),
                player_.clip_size());
  engine::hud_draw_text_right(buf, float(width_) - margin_x, baseline_y,
                              k_hud_red);

  constexpr float dot = 3.f;
  engine::hud_draw_solid_rect((float(width_) - dot) * 0.5f,
                              (float(height_) - dot) * 0.5f, dot, dot,
                              0xffffffffu);
}

void GameState::sys_render_debug_text() {
  bgfx::dbgTextClear();
  bgfx::dbgTextPrintf(0, 1, 0x0f,
                      "WASD  Mouse  Esc: %s   G: gizmo=%s   C: collision=%s   "
                      "L: lights=%s   LMB:Shoot R:Reload",
                      player_.mouse_look() ? "free cursor" : "capture",
                      player_.show_axes_gizmo() ? "on" : "off",
                      show_collision_ ? "on" : "off",
                      show_lights_ ? "on" : "off");
  bgfx::dbgTextPrintf(0, 2, 0x0f, "level=%s  sectors=%zu walls=%zu stairs=%zu",
                      level_.name.empty() ? "(unnamed)" : level_.name.c_str(),
                      level_.sectors.size(), level_.walls.size(),
                      level_.stairs.size());
}
