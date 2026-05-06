#pragma once

#include "engine/audio.hpp"
#include "engine/collider.hpp"
#include "engine/rigged_model.hpp"
#include "game/animator.hpp"
#include "game/fps_camera.hpp"
#include "game/physics_world.hpp"

#include <SDL.h>
#include <bgfx/bgfx.h>

namespace engine { struct Level; }

namespace game {

class Player {
public:
    void init(float spawn_x, float spawn_y, float spawn_z, float spawn_yaw,
              engine::RiggedModel* viewmodel,
              engine::SoundId      shot_sound,
              engine::SoundId      step_sound);

    void handle_event(const SDL_Event& event);
    void update(float dt, const engine::Level& level);

    // Attempt to fire. Consumes the shoot input; returns true and fills the
    // normalised ray direction if a shot actually happened.
    bool try_fire(float& fx, float& fy, float& fz);

    void render_viewmodel(
        bgfx::ViewId        view_id,
        bgfx::ProgramHandle skinned_program,
        bgfx::UniformHandle u_bones,
        bgfx::UniformHandle s_albedo,
        bgfx::UniformHandle u_baseColor,
        bgfx::TextureHandle white_tex,
        uint64_t            state,
        bgfx::ProgramHandle debug_program
    ) const;

    void take_damage(int amount);
    bool alive() const { return health_ > 0; }

    const FpsCamera& camera()  const { return camera_; }
    FpsCamera&       camera()        { return camera_; }

    int  health()          const { return health_; }
    int  bullets_in_clip() const { return bullets_in_clip_; }
    int  clip_size()       const { return clip_size_; }
    bool mouse_look()      const { return mouse_look_; }
    bool show_axes_gizmo() const { return show_axes_gizmo_; }

    const engine::Collider& collider() const { return collider_; }

private:
    void update_viewmodel_anim(float dt);

    FpsCamera             camera_{};
    engine::PlayerPhysics physics_{};
    engine::Collider      collider_{
        engine::PlayerPhysics::k_body_radius_xz,
        engine::PlayerPhysics::k_eye_height,
        engine::PlayerPhysics::k_step_up
    };

    int  health_          = 100;
    int  bullets_in_clip_ = 8;
    int  clip_size_       = 8;
    bool is_reloading_    = false;
    bool is_walking_      = false;
    bool mouse_look_      = true;
    bool show_axes_gizmo_ = false;

    engine::RiggedModel* viewmodel_model_ = nullptr;
    Animator             viewmodel_anim_;

    engine::SoundId shot_sound_ = engine::k_invalid_sound;
    engine::SoundId step_sound_ = engine::k_invalid_sound;

    float mouse_rel_x_   = 0.f;
    float mouse_rel_y_   = 0.f;
    bool  shoot_pressed_  = false;
    bool  reload_pressed_ = false;
};

} // namespace game
