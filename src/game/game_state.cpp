#include "game/game_state.hpp"

#include "engine/audio.hpp"
#include "engine/geometry/primitives.hpp"
#include "engine/hud.hpp"
#include "engine/lit_vertex.hpp"
#include "engine/physics/raycast.hpp"
#include "engine/render/buffers.hpp"
#include "engine/log.hpp"
#include "engine/shader_program.hpp"
#include "engine/texture_loader.hpp"
#include "game/level/level_loader.hpp"
#include "game/level/level_mesh.hpp"

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
  {
    std::string err;
    if (!engine::audio_init(err)) {
      LOG_WARN("audio: %s (continuing without sound)", err.c_str());
    } else {
      shot_sound_ =
          engine::audio_load(ENGINE_SOUNDS_DIR "/pistol_shot.mp3", err);
      if (shot_sound_ == engine::k_invalid_sound)
        LOG_WARN("audio: %s", err.c_str());
      step_sound_ = engine::audio_load(ENGINE_SOUNDS_DIR "/footsteps.mp3", err);
      if (step_sound_ == engine::k_invalid_sound)
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

  // Level geometry
  {
    engine::LevelMeshOutput meshes;
    engine::build_level_meshes(level_, meshes);
    if (meshes.floor_vertices.empty()) {
      LOG_ERROR("level has no floor geometry (sectors empty?)");
      return false;
    }

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
  s_albedo_ = bgfx::createUniform("s_albedo", bgfx::UniformType::Sampler);
  u_bones_ = bgfx::createUniform("u_bones", bgfx::UniformType::Mat4, 120);
  u_baseColor_ = bgfx::createUniform("u_baseColor", bgfx::UniformType::Vec4);

  // Textures
  static constexpr uint32_t k_white = 0xffffffffu;
  white_tex_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                                     0, bgfx::copy(&k_white, sizeof(k_white)));
  floor_tex_ = engine::load_texture_from_file(
      ENGINE_TEXTURES_DIR "/checkered_pavement_tiles_diff_2k.jpg");
  wall_tex_ = engine::load_texture_from_file(ENGINE_TEXTURES_DIR
                                             "/plastered_wall_04_diff_2k.jpg");

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

  // Target cube VBHs (one per damage tint, shared IB)
  {
    constexpr uint32_t tints[k_target_max_hits] = {
        0xff5050ffu,
        0xff3030c0u,
        0xff1a1a80u,
    };
    std::vector<LitVertex> verts;
    std::vector<uint16_t> indices;
    for (int t = 0; t < k_target_max_hits; ++t) {
      engine::build_unit_cube(verts, indices, 0.4f, tints[t]);
      cube_vbh_[t] = bgfx::createVertexBuffer(
          bgfx::copy(verts.data(), uint32_t(verts.size() * sizeof(LitVertex))),
          layout_);
      if (t == 0) {
        cube_ibh_ = bgfx::createIndexBuffer(bgfx::copy(
            indices.data(), uint32_t(indices.size() * sizeof(uint16_t))));
      }
    }
  }

  render_state_ = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                  BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS |
                  BGFX_STATE_MSAA;

  // Camera at spawn
  camera_.eyeX = level_.spawn.pos.x;
  camera_.eyeY =
      level_.spawn.pos.y + engine::PlayerPhysics::k_eye_height + 0.02f;
  camera_.eyeZ = level_.spawn.pos.z;
  camera_.yaw = level_.spawn.yaw_deg * (bx::kPi / 180.0f);

  if (SDL_SetRelativeMouseMode(SDL_TRUE) != 0)
    LOG_WARN("SDL_SetRelativeMouseMode: %s", SDL_GetError());

  // Player viewmodel (gun)
  {
    std::string err;
    viewmodel_model_ =
        load_model(ENGINE_MODELS_DIR "/animated_pistol-v2.glb", err);
    if (!viewmodel_model_) {
      LOG_WARN("viewmodel: %s", err.c_str());
    } else {
      viewmodel_anim_.bind(*viewmodel_model_);
      viewmodel_anim_.play(*viewmodel_model_, game::ViewmodelAnim::Take, false,
                           true);
      viewmodel_anim_.play(*viewmodel_model_, game::ViewmodelAnim::Idle, true,
                           false);
    }
  }

  // Spawn initial entities
  spawn_zombie(20.0f, 0.0f, 20.0f);
  spawn_zombie(4.0f, 0.0f, 7.0f);
  spawn_target(3.0f, 0.4f, 5.0f);
  spawn_target(4.0f, 0.4f, 5.5f);
  spawn_target(5.0f, 0.4f, 5.0f);

  return true;
}

// ============================================================
// shutdown
// ============================================================

void GameState::shutdown() {
  if (mouse_look_)
    SDL_SetRelativeMouseMode(SDL_FALSE);

  engine::audio_shutdown();
  engine::hud_shutdown();

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
  for (int t = 0; t < k_target_max_hits; ++t)
    dvb(cube_vbh_[t]);
  dib(cube_ibh_);

  dth(floor_tex_);
  dth(wall_tex_);
  dth(white_tex_);

  duh(u_light_pos_);
  duh(u_light_color_);
  duh(u_light_params_);
  duh(u_ambient_);
  duh(s_albedo_);
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

entt::entity GameState::spawn_zombie(float x, float y, float z) {
  std::string err;
  engine::RiggedModel *m =
      load_model(ENGINE_MODELS_DIR "/ZombieCity01_Shirt.glb", err);
  if (!m)
    LOG_WARN("zombie model: %s", err.c_str());

  auto e = registry_.create();
  auto &tr = registry_.emplace<game::TransformC>(e);
  tr.x = x;
  tr.y = y;
  tr.z = z;
  tr.yaw = 0.f;
  registry_.emplace<game::HealthC>(e);
  registry_.emplace<game::ZombieAIC>(e);
  auto &sm = registry_.emplace<game::SkinnedModelC>(e);
  sm.model = m;
  if (m) {
    sm.anim.bind(*m);
    sm.anim.play(*m, game::ZombieAnim::Idle, true, true);
  }
  registry_.emplace<game::ZombieTag>(e);
  return e;
}

entt::entity GameState::spawn_target(float x, float y, float z) {
  auto e = registry_.create();
  auto &tr = registry_.emplace<game::TransformC>(e);
  tr.x = x;
  tr.y = y;
  tr.z = z;
  tr.yaw = 0.f;
  registry_.emplace<game::TargetC>(e);
  registry_.emplace<game::TargetTag>(e);
  return e;
}

// ============================================================
// Event handling
// ============================================================

void GameState::on_resize(int width, int height) {
  width_ = width;
  height_ = height;
}

void GameState::handle_event(const SDL_Event &event) {
  switch (event.type) {
  case SDL_KEYDOWN:
    if (event.key.keysym.sym == SDLK_ESCAPE) {
      mouse_look_ = !mouse_look_;
      SDL_SetRelativeMouseMode(mouse_look_ ? SDL_TRUE : SDL_FALSE);
    } else if (event.key.keysym.sym == SDLK_r && !event.key.repeat) {
      reload_pressed_ = true;
    } else if (event.key.keysym.sym == SDLK_g && !event.key.repeat) {
      show_axes_gizmo_ = !show_axes_gizmo_;
    }
    break;
  case SDL_MOUSEBUTTONDOWN:
    if (event.button.button == SDL_BUTTON_LEFT && mouse_look_)
      shoot_pressed_ = true;
    break;
  case SDL_MOUSEMOTION:
    if (mouse_look_) {
      mouse_rel_x_ += static_cast<float>(event.motion.xrel);
      mouse_rel_y_ += static_cast<float>(event.motion.yrel);
    }
    break;
  default:
    break;
  }
}

// ============================================================
// Update
// ============================================================

void GameState::update(float dt) {
  // Mouse look
  if (mouse_look_)
    fps_camera_apply_mouse(camera_, mouse_rel_x_, mouse_rel_y_, 0.0025f);
  mouse_rel_x_ = 0.f;
  mouse_rel_y_ = 0.f;

  // WASD movement + physics
  const float prev_x = camera_.eyeX;
  const float prev_z = camera_.eyeZ;
  {
    const uint8_t *keys = SDL_GetKeyboardState(nullptr);
    float dx = 0.f, dy = 0.f, dz = 0.f;
    fps_camera_apply_wasd(camera_, keys, dt, 5.0f, dx, dy, dz);
    camera_.eyeX += dx;
    camera_.eyeY += dy;
    camera_.eyeZ += dz;
  }
  engine::player_physics_step(camera_, player_physics_, dt, level_, prev_x,
                              prev_z);

  // Footsteps follow actual post-physics movement, not key state.
  {
    const float mdx = camera_.eyeX - prev_x;
    const float mdz = camera_.eyeZ - prev_z;
    const float min_step = 0.5f * dt;
    is_walking_ = (mdx * mdx + mdz * mdz) > (min_step * min_step);
    engine::audio_set_looping(step_sound_, is_walking_);
  }

  sys_shooting();

  // Reload: set flag, play animation on first frame
  {
    const bool can_reload = !is_reloading_ && bullets_in_clip_ < clip_size_;
    if (!can_reload)
      reload_pressed_ = false;
    if (reload_pressed_)
      is_reloading_ = true;
  }

  sys_viewmodel_anim(dt);
  sys_zombie_ai(dt);

  // Consume frame inputs
  shoot_pressed_ = false;
  reload_pressed_ = false;
}

// --- system: shoot ---

void GameState::sys_shooting() {
  if (!shoot_pressed_)
    return;
  const bool can_shoot = !is_reloading_ && bullets_in_clip_ > 0;
  if (!can_shoot) {
    shoot_pressed_ = false;
    return;
  }

  engine::audio_play(shot_sound_);
  --bullets_in_clip_;

  const float fx = std::cos(camera_.pitch) * std::sin(camera_.yaw);
  const float fy = std::sin(camera_.pitch);
  const float fz = std::cos(camera_.pitch) * std::cos(camera_.yaw);

  float best_t = std::numeric_limits<float>::infinity();
  engine::ray_walls_nearest(level_.walls, camera_.eyeX, camera_.eyeY,
                            camera_.eyeZ, fx, fy, fz, best_t);

  // Targets: find closest, damage it.
  {
    entt::entity best_e = entt::null;
    for (auto entity : registry_.view<game::TargetTag>()) {
      auto &tgt = registry_.get<game::TargetC>(entity);
      auto &tr = registry_.get<game::TransformC>(entity);
      if (!tgt.alive)
        continue;
      const float h = tgt.half_extent;
      float t_hit;
      if (engine::ray_aabb(camera_.eyeX, camera_.eyeY, camera_.eyeZ, fx, fy, fz,
                           tr.x - h, tr.y - h, tr.z - h, tr.x + h, tr.y + h,
                           tr.z + h, t_hit) &&
          t_hit < best_t) {
        best_t = t_hit;
        best_e = entity;
      }
    }
    if (best_e != entt::null) {
      auto &tgt = registry_.get<game::TargetC>(best_e);
      if (--tgt.hits_remaining <= 0)
        tgt.alive = false;
    }
  }

  // Zombies: test capsule, only if closer than wall/target.
  for (auto entity : registry_.view<game::ZombieTag>()) {
    auto &tr = registry_.get<game::TransformC>(entity);
    auto &hp = registry_.get<game::HealthC>(entity);
    auto &ai = registry_.get<game::ZombieAIC>(entity);
    auto &sm = registry_.get<game::SkinnedModelC>(entity);
    if (!hp.alive() || !sm.model)
      continue;

    const float ca[3] = {tr.x, tr.y + game::ZombieAIC::k_radius, tr.z};
    const float cb[3] = {
        tr.x, tr.y + game::ZombieAIC::k_height - game::ZombieAIC::k_radius,
        tr.z};
    float t_hit;
    if (engine::ray_capsule(camera_.eyeX, camera_.eyeY, camera_.eyeZ, fx, fy,
                            fz, ca[0], ca[1], ca[2], cb[0], cb[1], cb[2],
                            game::ZombieAIC::k_radius, t_hit) &&
        t_hit < best_t) {
      best_t = t_hit;
      hp.hp -= 10;
      if (hp.alive() && std::rand() % 2 == 0) {
        sm.anim.play(*sm.model, game::ZombieAnim::GetHit, false, true);
        ai.hit_stun_timer = 0.5f;
      }
    }
  }

  // Trigger viewmodel shoot animation.
  if (viewmodel_model_ && viewmodel_model_->valid())
    viewmodel_anim_.play(*viewmodel_model_, game::ViewmodelAnim::Shoot, false,
                         true);
}

// --- system: viewmodel animation ---

void GameState::sys_viewmodel_anim(float dt) {
  if (!viewmodel_model_)
    return;
  auto &vm = *viewmodel_model_;
  if (!vm.valid())
    return;

  const game::ViewmodelAnim cur = viewmodel_anim_.current_vm(vm);

  // Reload completion
  if (is_reloading_ && cur == game::ViewmodelAnim::Reload &&
      vm.current_finished()) {
    bullets_in_clip_ = clip_size_;
    is_reloading_ = false;
  }

  if (reload_pressed_ && is_reloading_) {
    viewmodel_anim_.play(vm, game::ViewmodelAnim::Reload, false, true);
  } else {
    const bool one_shot = cur == game::ViewmodelAnim::Shoot ||
                          cur == game::ViewmodelAnim::Reload ||
                          cur == game::ViewmodelAnim::Take;
    if (one_shot && vm.current_finished())
      viewmodel_anim_.play(vm, game::ViewmodelAnim::Idle, true, false);
  }

  vm.update(dt);
}

// --- system: zombie AI ---

void GameState::sys_zombie_ai(float dt) {
  constexpr float k_walk_speed = 1.5f;
  constexpr float k_engage_dist = 1.2f;
  constexpr float k_damage_interval = 1.0f;
  constexpr int k_damage_per_hit = 10;
  constexpr float k_min_sep =
      game::ZombieAIC::k_radius + engine::PlayerPhysics::k_body_radius_xz;

  for (auto entity : registry_.view<game::ZombieTag>()) {
    auto &tr = registry_.get<game::TransformC>(entity);
    auto &hp = registry_.get<game::HealthC>(entity);
    auto &ai = registry_.get<game::ZombieAIC>(entity);
    auto &sm = registry_.get<game::SkinnedModelC>(entity);

    if (!sm.model)
      continue;
    auto &model = *sm.model;
    if (!model.valid())
      continue;

    if (!hp.alive()) {
      if (!ai.dying) {
        ai.dying = true;
        sm.anim.play(model, game::ZombieAnim::Death, false, true);
      }
      model.update(dt);
      continue;
    }

    const float pdx = camera_.eyeX - tr.x;
    const float pdz = camera_.eyeZ - tr.z;
    const float dist_xz = std::sqrt(pdx * pdx + pdz * pdz);

    tr.yaw = std::atan2(-pdx, pdz);

    if (ai.hit_stun_timer > 0.f) {
      ai.hit_stun_timer -= dt;
      if (ai.hit_stun_timer < 0.f)
        ai.hit_stun_timer = 0.f;
    } else if (dist_xz > k_engage_dist) {
      ai.damage_timer = 0.f;
      const float inv = 1.0f / dist_xz;
      const float cand_x = tr.x + pdx * inv * k_walk_speed * dt;
      const float cand_z = tr.z + pdz * inv * k_walk_speed * dt;
      engine::resolve_xz_slide(level_, tr.x, tr.z, cand_x, cand_z, tr.y,
                               game::ZombieAIC::k_radius,
                               engine::PlayerPhysics::k_step_up, tr.x, tr.z);
      sm.anim.play(model, game::ZombieAnim::Walk, true, false);
    } else {
      ai.damage_timer += dt;
      if (ai.damage_timer >= k_damage_interval) {
        ai.damage_timer -= k_damage_interval;
        player_health_ = std::max(0, player_health_ - k_damage_per_hit);
      }
      if (sm.anim.current_z(model) != game::ZombieAnim::Attack ||
          model.current_finished()) {
        sm.anim.play_random(model, game::ZombieAnim::Attack, false);
      }
    }

    // Push overlapping bodies apart.
    const float sep_dx = tr.x - camera_.eyeX;
    const float sep_dz = tr.z - camera_.eyeZ;
    const float sep_dist = std::sqrt(sep_dx * sep_dx + sep_dz * sep_dz);
    if (sep_dist > 0.f && sep_dist < k_min_sep) {
      const float half_push = 0.5f * (k_min_sep - sep_dist) / sep_dist;
      const float prev_zx = tr.x;
      const float prev_zz = tr.z;
      tr.x += sep_dx * half_push;
      tr.z += sep_dz * half_push;
      camera_.eyeX -= sep_dx * half_push;
      camera_.eyeZ -= sep_dz * half_push;
      engine::resolve_xz_slide(level_, prev_zx, prev_zz, tr.x, tr.z, tr.y,
                               game::ZombieAIC::k_radius,
                               engine::PlayerPhysics::k_step_up, tr.x, tr.z);
    }

    model.update(dt);
  }
}

// ============================================================
// Render
// ============================================================

void GameState::render(int width, int height) {
  width_ = width;
  height_ = height;

  const float aspect = height > 0 ? float(width) / float(height) : 1.f;
  float view[16], proj[16];
  fps_camera_view_proj(camera_, aspect, bgfx::getCaps()->homogeneousDepth, view,
                       proj);

  bgfx::setViewTransform(0, view, proj);
  bgfx::setViewRect(0, 0, 0, uint16_t(width), uint16_t(height));
  bgfx::setViewRect(1, 0, 0, uint16_t(width), uint16_t(height));
  bgfx::setViewTransform(1, view, proj);

  sys_set_lights();
  sys_render_level();
  sys_render_targets();
  sys_render_characters();
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
    pos_pack[3] = 0.f;
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
}

void GameState::sys_render_level() {
  float mtx[16];

  auto submit_geo = [&](bgfx::VertexBufferHandle vbh, bgfx::TextureHandle tex) {
    if (!bgfx::isValid(vbh))
      return;
    bgfx::setState(render_state_);
    bx::mtxIdentity(mtx);
    bgfx::setTransform(mtx);
    bgfx::setTexture(0, s_albedo_, tex);
    bgfx::setVertexBuffer(0, vbh);
    bgfx::submit(0, program_);
  };

  const bgfx::TextureHandle floor_bind =
      bgfx::isValid(floor_tex_) ? floor_tex_ : white_tex_;
  const bgfx::TextureHandle wall_bind =
      bgfx::isValid(wall_tex_) ? wall_tex_ : white_tex_;
  submit_geo(floor_vbh_, floor_bind);
  submit_geo(wall_vbh_, wall_bind);
  submit_geo(stair_vbh_, wall_bind);

  auto draw_bulb = [&](float lx, float ly, float lz) {
    bgfx::setState(render_state_);
    bx::mtxTranslate(mtx, lx, ly, lz);
    bgfx::setTransform(mtx);
    bgfx::setTexture(0, s_albedo_, white_tex_);
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

void GameState::sys_render_targets() {
  float mtx[16];
  for (auto entity : registry_.view<game::TargetTag>()) {
    const auto &tgt = registry_.get<game::TargetC>(entity);
    const auto &tr = registry_.get<game::TransformC>(entity);
    if (!tgt.alive)
      continue;
    const int tint = std::clamp(k_target_max_hits - tgt.hits_remaining, 0,
                                k_target_max_hits - 1);
    bgfx::setState(render_state_);
    bx::mtxTranslate(mtx, tr.x, tr.y, tr.z);
    bgfx::setTransform(mtx);
    bgfx::setTexture(0, s_albedo_, white_tex_);
    bgfx::setVertexBuffer(0, cube_vbh_[tint]);
    bgfx::setIndexBuffer(cube_ibh_);
    bgfx::submit(0, program_);
  }
}

void GameState::sys_render_characters() {
  if (!bgfx::isValid(skinned_program_))
    return;

  // Player viewmodel
  if (viewmodel_model_ && viewmodel_model_->valid()) {
    engine::ViewmodelDrawParams vmp{};
    vmp.eye[0] = camera_.eyeX;
    vmp.eye[1] = camera_.eyeY;
    vmp.eye[2] = camera_.eyeZ;
    vmp.yaw = -camera_.yaw;
    vmp.pitch = -camera_.pitch;
    vmp.offset[0] = 0.15f;
    vmp.offset[1] = -1.575f;
    vmp.offset[2] = 0.0f;
    vmp.scale = 1.f;
    viewmodel_model_->submit_viewmodel(1, skinned_program_, u_bones_, s_albedo_,
                                       u_baseColor_, white_tex_, render_state_,
                                       vmp);
    if (show_axes_gizmo_ && bgfx::isValid(debug_program_))
      viewmodel_model_->submit_axes_gizmo(1, debug_program_, vmp, 0.30f);
  }

  // World-space characters (zombies)
  for (auto entity : registry_.view<game::ZombieTag>()) {
    const auto &tr = registry_.get<game::TransformC>(entity);
    const auto &sm = registry_.get<game::SkinnedModelC>(entity);
    if (!sm.model || !sm.model->valid())
      continue;
    engine::CharacterDrawParams cdp{};
    cdp.pos[0] = tr.x;
    cdp.pos[1] = tr.y;
    cdp.pos[2] = tr.z;
    cdp.yaw = tr.yaw;
    cdp.scale = 1.f;
    sm.model->submit_world(0, skinned_program_, u_bones_, s_albedo_,
                           u_baseColor_, white_tex_, render_state_, cdp);
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
  std::snprintf(buf, sizeof(buf), "%d%%", player_health_);
  engine::hud_draw_text(buf, margin_x, baseline_y, k_hud_red);

  std::snprintf(buf, sizeof(buf), "%d/%d", bullets_in_clip_, clip_size_);
  engine::hud_draw_text_right(buf, float(width_) - margin_x, baseline_y,
                              k_hud_red);

  constexpr float dot = 3.f;
  engine::hud_draw_solid_rect((float(width_) - dot) * 0.5f,
                              (float(height_) - dot) * 0.5f, dot, dot,
                              0xffffffffu);
}

void GameState::sys_render_debug_text() {
  bgfx::dbgTextClear();
  bgfx::dbgTextPrintf(
      0, 1, 0x0f, "WASD  Mouse  Esc: %s   G: gizmo=%s   LMB:Shoot R:Reload",
      mouse_look_ ? "free cursor" : "capture", show_axes_gizmo_ ? "on" : "off");
  bgfx::dbgTextPrintf(0, 2, 0x0f, "level=%s  sectors=%zu walls=%zu stairs=%zu",
                      level_.name.empty() ? "(unnamed)" : level_.name.c_str(),
                      level_.sectors.size(), level_.walls.size(),
                      level_.stairs.size());
}
