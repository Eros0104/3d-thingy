#pragma once

#include "engine/audio.hpp"
#include "engine/rigged_model.hpp"
#include "game/animator.hpp"
#include "game/fps_camera.hpp"
#include "game/zombie.hpp"
#include "game/level/level_data.hpp"
#include "game/physics_world.hpp"

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
    void spawn_zombie(float x, float y, float z);
    entt::entity spawn_target(float x, float y, float z);

    // Loads a GLB into the model pool; returns a non-owning pointer or nullptr on failure.
    engine::RiggedModel* load_model(const char* path, std::string& err);

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

    // Owned model pool — RiggedModel is non-copyable so stored via unique_ptr.
    std::vector<std::unique_ptr<engine::RiggedModel>> models_;

    std::vector<game::Zombie> zombies_;

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

    // Player viewmodel (gun).
    engine::RiggedModel* viewmodel_model_ = nullptr;
    game::Animator       viewmodel_anim_;

    // --- per-frame input (accumulated in handle_event, consumed in update) ---
    float mouse_rel_x_   = 0.f;
    float mouse_rel_y_   = 0.f;
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
