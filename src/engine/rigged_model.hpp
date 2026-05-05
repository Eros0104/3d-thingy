#pragma once

#include <bgfx/bgfx.h>
#include <string>

namespace engine {

struct ViewmodelDrawParams {
    float eye[3];
    float yaw, pitch;
    float offset[3];
    float tweak_pitch, tweak_yaw, tweak_roll;
    float scale;
};

struct CharacterDrawParams {
    float pos[3];
    float yaw;
    float scale;
};

class RiggedModel {
public:
    RiggedModel();
    ~RiggedModel();

    RiggedModel(const RiggedModel&)            = delete;
    RiggedModel& operator=(const RiggedModel&) = delete;

    bool load(const char* glb_path, std::string& err);
    void unload();
    bool valid() const;

    // Raw animation control — index into the model's animation list.
    // crossfade=true blends from the current pose over a short window.
    void play_raw(int idx, bool loop, bool restart_if_same, bool crossfade = false);
    int         current_raw()      const;
    bool        current_finished() const;
    int         anim_count()       const;
    const char* anim_name(int i)   const;

    void update(float dt);

    void submit_viewmodel(
        bgfx::ViewId         view_id,
        bgfx::ProgramHandle  program,
        bgfx::UniformHandle  u_bones,
        bgfx::UniformHandle  s_albedo,
        bgfx::UniformHandle  u_baseColor,
        bgfx::TextureHandle  fallback_white,
        uint64_t             state,
        const ViewmodelDrawParams& params
    );

    void submit_world(
        bgfx::ViewId         view_id,
        bgfx::ProgramHandle  program,
        bgfx::UniformHandle  u_bones,
        bgfx::UniformHandle  s_albedo,
        bgfx::UniformHandle  u_baseColor,
        bgfx::TextureHandle  fallback_white,
        uint64_t             state,
        const CharacterDrawParams& params
    );

    void submit_axes_gizmo(
        bgfx::ViewId        view_id,
        bgfx::ProgramHandle debug_program,
        const ViewmodelDrawParams& params,
        float               length
    );

private:
    struct State;
    State* s_ = nullptr;
};

} // namespace engine
