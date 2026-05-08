#pragma once

#include "engine/model.hpp"
#include "engine/physics/jolt_physics.hpp"
#include <bgfx/bgfx.h>
#include <string>

class Door {
public:
    Door();
    ~Door();

    Door(const Door&)            = delete;
    Door& operator=(const Door&) = delete;

    bool load(const char* frame_path, const char* leaf_path, std::string& err);
    void unload();
    bool valid() const;

    void open();
    void close();
    bool is_open() const;

    // Call once after load() to attach a kinematic collision body.
    // half_w/h/d are the leaf half-extents in world units (default: standard interior door).
    void init_jolt(engine::JoltPhysics& jolt, const engine::ModelDrawParams& params,
                   float half_w = 0.45f, float half_h = 1.05f, float half_d = 0.025f);

    void update(float dt);

    void submit_world(
        bgfx::ViewId         view_id,
        bgfx::ProgramHandle  program,
        bgfx::UniformHandle  s_albedo,
        bgfx::UniformHandle  s_normal,
        bgfx::UniformHandle  s_roughness,
        bgfx::TextureHandle  fallback_albedo,
        bgfx::TextureHandle  fallback_normal,
        bgfx::TextureHandle  fallback_roughness,
        uint64_t             state,
        const engine::ModelDrawParams& params
    );

private:
    engine::Model        frame_;
    engine::Model        leaf_;
    float                leaf_angle_   = 0.0f;
    float                target_angle_ = 0.0f;
    bool                 open_         = false;

    engine::JoltPhysics* jolt_         = nullptr;
    engine::BodyHandle   body_         = engine::k_invalid_body;
    float                hinge_pos_[3] = {0.0f, 0.0f, 0.0f};
    float                base_yaw_     = 0.0f;
};
