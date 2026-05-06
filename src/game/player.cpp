#include "game/player.hpp"

#include "engine/audio.hpp"
#include "engine/physics/jolt_physics.hpp"

#include <SDL.h>
#include <bx/math.h>

#include <algorithm>
#include <cmath>

namespace game {

void Player::init(float sx, float spawn_eye_y, float sz, float syaw,
                  engine::JoltPhysics&  jolt,
                  engine::RiggedModel* viewmodel,
                  engine::SoundId      shot_sound,
                  engine::SoundId      step_sound)
{
    camera_.eyeX = sx;
    camera_.eyeY = spawn_eye_y;
    camera_.eyeZ = sz;
    camera_.yaw  = syaw;

    shot_sound_ = shot_sound;
    step_sound_ = step_sound;

    viewmodel_model_ = viewmodel;
    if (viewmodel_model_) {
        viewmodel_anim_.bind(*viewmodel_model_);
        viewmodel_anim_.play(*viewmodel_model_, ViewmodelAnim::Take, false, true);
        viewmodel_anim_.play(*viewmodel_model_, ViewmodelAnim::Idle, true,  false);
    }

    const float feet_y = spawn_eye_y - k_eye_height;
    jolt_handle_ = jolt.add_character(sx, feet_y, sz, k_radius, k_eye_height, k_step_height);
}

void Player::handle_event(const SDL_Event& event) {
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

void Player::update(float dt, engine::JoltPhysics& jolt) {
    if (mouse_look_)
        fps_camera_apply_mouse(camera_, mouse_rel_x_, mouse_rel_y_, 0.0025f);
    mouse_rel_x_ = 0.f;
    mouse_rel_y_ = 0.f;

    // Compute desired horizontal velocity from WASD input.
    float dx = 0.f, dy_unused = 0.f, dz = 0.f;
    {
        const uint8_t* keys = SDL_GetKeyboardState(nullptr);
        fps_camera_apply_wasd(camera_, keys, dt, 5.0f, dx, dy_unused, dz);
    }
    // fps_camera_apply_wasd returns displacement (vel * dt); convert to velocity.
    const float inv_dt = (dt > 1e-6f) ? 1.f / dt : 0.f;
    const float vel_x  = dx * inv_dt;
    const float vel_z  = dz * inv_dt;

    const float prev_eye_x = camera_.eyeX;
    const float prev_eye_z = camera_.eyeZ;

    jolt.move_character(jolt_handle_, vert_state_, vel_x, vel_z, dt);

    // Sync camera from Jolt character position.
    float fx, fy, fz;
    jolt.get_character_pos(jolt_handle_, fx, fy, fz);
    camera_.eyeX = fx;
    camera_.eyeY = fy + k_eye_height;
    camera_.eyeZ = fz;

    {
        const float mdx     = camera_.eyeX - prev_eye_x;
        const float mdz     = camera_.eyeZ - prev_eye_z;
        const float min_step = 0.5f * dt;
        is_walking_ = (mdx * mdx + mdz * mdz) > (min_step * min_step);
        engine::audio_set_looping(step_sound_, is_walking_);
    }

    {
        const bool can_reload = !is_reloading_ && bullets_in_clip_ < clip_size_;
        if (!can_reload) reload_pressed_ = false;
        if (reload_pressed_) is_reloading_ = true;
    }

    update_viewmodel_anim(dt);
    reload_pressed_ = false;
}

bool Player::try_fire(float& fx, float& fy, float& fz) {
    if (!shoot_pressed_) return false;
    shoot_pressed_ = false;
    if (is_reloading_ || bullets_in_clip_ <= 0) return false;

    engine::audio_play(shot_sound_);
    --bullets_in_clip_;

    fx = std::cos(camera_.pitch) * std::sin(camera_.yaw);
    fy = std::sin(camera_.pitch);
    fz = std::cos(camera_.pitch) * std::cos(camera_.yaw);

    if (viewmodel_model_ && viewmodel_model_->valid())
        viewmodel_anim_.play(*viewmodel_model_, ViewmodelAnim::Shoot, false, true);

    return true;
}

void Player::update_viewmodel_anim(float dt) {
    if (!viewmodel_model_) return;
    auto& vm = *viewmodel_model_;
    if (!vm.valid()) return;

    const ViewmodelAnim cur = viewmodel_anim_.current_vm(vm);

    if (is_reloading_ && cur == ViewmodelAnim::Reload && vm.current_finished()) {
        bullets_in_clip_ = clip_size_;
        is_reloading_    = false;
    }

    if (reload_pressed_ && is_reloading_) {
        viewmodel_anim_.play(vm, ViewmodelAnim::Reload, false, true);
    } else {
        const bool one_shot = cur == ViewmodelAnim::Shoot ||
                              cur == ViewmodelAnim::Reload ||
                              cur == ViewmodelAnim::Take;
        if (one_shot && vm.current_finished())
            viewmodel_anim_.play(vm, ViewmodelAnim::Idle, true, false);
    }

    vm.update(dt);
}

void Player::render_viewmodel(
    bgfx::ViewId        view_id,
    bgfx::ProgramHandle skinned_program,
    bgfx::UniformHandle u_bones,
    bgfx::UniformHandle s_albedo,
    bgfx::UniformHandle u_baseColor,
    bgfx::TextureHandle white_tex,
    uint64_t            state,
    bgfx::ProgramHandle debug_program) const
{
    if (!viewmodel_model_ || !viewmodel_model_->valid()) return;

    engine::ViewmodelDrawParams vmp{};
    vmp.eye[0]    = camera_.eyeX;
    vmp.eye[1]    = camera_.eyeY;
    vmp.eye[2]    = camera_.eyeZ;
    vmp.yaw       = -camera_.yaw;
    vmp.pitch     = -camera_.pitch;
    vmp.offset[0] = 0.15f;
    vmp.offset[1] = -1.575f;
    vmp.offset[2] = 0.0f;
    vmp.scale     = 1.f;

    viewmodel_model_->submit_viewmodel(view_id, skinned_program, u_bones,
                                       s_albedo, u_baseColor, white_tex, state, vmp);

    if (show_axes_gizmo_ && bgfx::isValid(debug_program))
        viewmodel_model_->submit_axes_gizmo(view_id, debug_program, vmp, 0.30f);
}

void Player::take_damage(int amount) {
    health_ = std::max(0, health_ - amount);
}

} // namespace game
