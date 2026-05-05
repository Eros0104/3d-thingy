#pragma once

#include "engine/audio.hpp"
#include "engine/lit_vertex.hpp"
#include "game/components.hpp"
#include "game/fps_camera.hpp"
#include "game/level/level_data.hpp"
#include "game/physics_world.hpp"
#include "game/viewmodel.hpp"

#include <SDL.h>
#include <bgfx/bgfx.h>
#include <entt/entt.hpp>

#include <memory>
#include <string>
#include <vector>

class GameState {
public:
    GameState();
    ~GameState();

    GameState(const GameState&)            = delete;
    GameState& operator=(const GameState&) = delete;

    bool init(const char* level_path, int width, int height);
    void shutdown();

    void on_resize(int width, int height);
    void handle_event(const SDL_Event& event);
    void update(float dt);
    void render(int width, int height);

private:
    // --- entity factories ---
    entt::entity spawn_zombie(float x, float y, float z);
    entt::entity spawn_target(float x, float y, float z);

    // Load a GLB into the models pool; returns index or -1 on failure.
    int load_model(const char* path, std::string& err);

    // --- systems ---
    void sys_shooting();
    void sys_viewmodel_anim(float dt);
    void sys_zombie_ai(float dt);
    void sys_set_lights();
    void sys_render_level();
    void sys_render_targets();
    void sys_render_characters();
    void sys_render_hud();
    void sys_render_debug_text();

    // --- ECS ---
    entt::registry registry_;

    // Owned model pool — Viewmodel is non-copyable so we store unique_ptrs.
    std::vector<std::unique_ptr<engine::Viewmodel>> models_;

    // --- player (singleton, not an entity) ---
    FpsCamera             camera_{};
    engine::PlayerPhysics player_physics_{};
    int  player_health_   = 100;
    int  bullets_in_clip_ = 8;
    int  clip_size_       = 8;
    bool is_reloading_    = false;
    bool is_walking_      = false;
    bool mouse_look_      = true;
    bool show_axes_gizmo_ = false;

    // Index of the player's gun model in models_ (-1 if not loaded).
    int viewmodel_idx_ = -1;

    // --- per-frame input (accumulated in handle_event, consumed in update) ---
    float mouse_rel_x_  = 0.f;
    float mouse_rel_y_  = 0.f;
    bool  shoot_pressed_  = false;
    bool  reload_pressed_ = false;

    // --- level ---
    engine::Level level_{};

    // --- bgfx resources ---
    static constexpr int k_max_shader_lights = 8;
    static constexpr int k_target_max_hits   = 3;

    bgfx::VertexLayout       layout_{};
    bgfx::VertexBufferHandle floor_vbh_  = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle wall_vbh_   = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle stair_vbh_  = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle bulb_vbh_   = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle  bulb_ibh_   = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle cube_vbh_[k_target_max_hits]{};
    bgfx::IndexBufferHandle  cube_ibh_   = BGFX_INVALID_HANDLE;

    bgfx::ProgramHandle program_         = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle skinned_program_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle debug_program_   = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle hud_program_     = BGFX_INVALID_HANDLE;

    bgfx::UniformHandle u_light_pos_    = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_light_color_  = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_light_params_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_ambient_      = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_albedo_       = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_bones_        = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_baseColor_    = BGFX_INVALID_HANDLE;

    bgfx::TextureHandle white_tex_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle floor_tex_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle wall_tex_  = BGFX_INVALID_HANDLE;

    // --- audio ---
    engine::SoundId shot_sound_  = engine::k_invalid_sound;
    engine::SoundId step_sound_  = engine::k_invalid_sound;

    bool     hud_ok_       = false;
    uint64_t render_state_ = 0;
    int      width_        = 1280;
    int      height_       = 720;
};
