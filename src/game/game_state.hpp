#pragma once

#include "engine/material.hpp"
#include "engine/physics/jolt_physics.hpp"
#include "engine/rigged_model.hpp"
#include "game/door.hpp"
#include "game/level/level_data.hpp"
#include "game/particles.hpp"
#include "game/player.hpp"
#include "game/zombie.hpp"

#include <SDL.h>
#include <bgfx/bgfx.h>

#include <memory>
#include <string>
#include <vector>

class GameState {
public:
  GameState();
  ~GameState();

  GameState(const GameState &) = delete;
  GameState &operator=(const GameState &) = delete;

  bool init(const char *level_path, int width, int height);
  void shutdown();

  void on_resize(int width, int height);
  void handle_event(const SDL_Event &event);
  void update(float dt);
  void render(int width, int height);

private:
  void spawn_zombie(float x, float y, float z);
  void spawn_target(float x, float y, float z);

  engine::RiggedModel *load_model(const char *path, std::string &err);

  void sys_shooting();
  void sys_zombie_ai(float dt);
  void sys_interact_door();
  void sys_set_lights();
  void sys_render_level();
  void sys_render_targets();
  void sys_render_characters();
  void sys_render_bullet_holes();
  void sys_render_collision_debug();
  void sys_render_debug_lights();
  void sys_render_wireframe();
  void sys_render_hud();
  void sys_render_debug_text();

  struct BulletHole {
    float x, y, z, nx, ny, nz;
  };

  // Owned model pool — RiggedModel is non-copyable so stored via unique_ptr.
  std::vector<std::unique_ptr<engine::RiggedModel>> models_;

  engine::JoltPhysics jolt_;
  game::Player player_;
  std::vector<game::Zombie> zombies_;
  game::BloodParticleSystem particles_;
  Door door_;
  engine::ModelDrawParams door_params_{};
  bool near_door_         = false;
  bool interact_pressed_  = false;
  std::vector<BulletHole> bullet_holes_;

  // --- level ---
  engine::Level level_{};

  // --- bgfx resources ---
  static constexpr int k_max_shader_lights = 8;
  static constexpr int k_max_shader_walls = 128;

  bgfx::VertexLayout layout_{};
  bgfx::VertexBufferHandle floor_vbh_ = BGFX_INVALID_HANDLE;
  bgfx::VertexBufferHandle wall_vbh_ = BGFX_INVALID_HANDLE;
  bgfx::VertexBufferHandle stair_vbh_ = BGFX_INVALID_HANDLE;
  bgfx::VertexBufferHandle bulb_vbh_ = BGFX_INVALID_HANDLE;
  bgfx::VertexBufferHandle wf_wall_vbh_ = BGFX_INVALID_HANDLE;
  bgfx::VertexBufferHandle wf_floor_vbh_ = BGFX_INVALID_HANDLE;
  bgfx::VertexBufferHandle wf_stair_vbh_ = BGFX_INVALID_HANDLE;
  bgfx::VertexBufferHandle wf_clip_vbh_ = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle bulb_ibh_ = BGFX_INVALID_HANDLE;
  bgfx::IndexBufferHandle cube_ibh_ = BGFX_INVALID_HANDLE;

  bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle skinned_program_ = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle debug_program_ = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle hud_program_ = BGFX_INVALID_HANDLE;

  bgfx::UniformHandle u_light_pos_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_light_color_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_light_params_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_ambient_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_cam_pos_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_wall_segs_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_wall_params_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_albedo_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_normal_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_roughness_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_bones_ = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle u_baseColor_ = BGFX_INVALID_HANDLE;

  bgfx::TextureHandle white_tex_ = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle flat_normal_tex_ =
      BGFX_INVALID_HANDLE; // 1x1 (128,128,255)
  bgfx::TextureHandle bullet_hole_tex_ = BGFX_INVALID_HANDLE;

  engine::Material floor_mat_{};
  engine::Material wall_mat_{};

  bool hud_ok_ = false;
  bool show_collision_ = false;
  bool show_lights_ = false;
  bool show_wireframe_ = false;
  uint64_t render_state_ = 0;
  int width_ = 1280;
  int height_ = 720;
};
