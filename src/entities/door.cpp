#include "entities/door.hpp"
#include <cmath>

static constexpr float k_open_angle = 1.5707963f; // pi/2 radians
static constexpr float k_speed      = 2.5f;        // radians/sec — ~0.6s to full open

Door::Door()  = default;
Door::~Door() { unload(); }

bool Door::load(const char* frame_path, const char* leaf_path, std::string& err)
{
    if (!frame_.load(frame_path, err)) return false;
    if (!leaf_.load(leaf_path,   err)) return false;
    return true;
}

void Door::unload()
{
    if (jolt_ && body_ != engine::k_invalid_body) {
        jolt_->remove_body(body_);
        body_ = engine::k_invalid_body;
    }
    jolt_ = nullptr;

    frame_.unload();
    leaf_.unload();
    leaf_angle_   = 0.0f;
    target_angle_ = 0.0f;
    open_         = false;
}

void Door::init_jolt(engine::JoltPhysics& jolt, const engine::ModelDrawParams& params,
                     float half_w, float half_h, float half_d)
{
    jolt_           = &jolt;
    hinge_pos_[0]   = params.pos[0];
    hinge_pos_[1]   = params.pos[1];
    hinge_pos_[2]   = params.pos[2];
    base_yaw_       = params.yaw;
    // Box center is (half_w, half_h, 0) in local body space — offset from the hinge edge.
    body_ = jolt.add_kinematic_box(
        params.pos[0], params.pos[1], params.pos[2], params.yaw,
        half_w, half_h, half_d,
        half_w, half_h, 0.0f
    );
}

bool Door::valid()   const { return frame_.valid() && leaf_.valid(); }
bool Door::is_open() const { return open_; }

void Door::open()
{
    open_         = true;
    target_angle_ = k_open_angle;
}

void Door::close()
{
    open_         = false;
    target_angle_ = 0.0f;
}

void Door::update(float dt)
{
    float diff = target_angle_ - leaf_angle_;
    float step = k_speed * dt;
    if (std::abs(diff) <= step)
        leaf_angle_ = target_angle_;
    else
        leaf_angle_ += (diff > 0.0f ? step : -step);

    if (jolt_ && body_ != engine::k_invalid_body)
        jolt_->set_kinematic_transform(body_,
            hinge_pos_[0], hinge_pos_[1], hinge_pos_[2],
            base_yaw_ + leaf_angle_);
}

const float doorScale = 0.1;

void Door::submit_world(
    bgfx::ViewId view_id, bgfx::ProgramHandle program,
    bgfx::UniformHandle s_albedo, bgfx::UniformHandle s_normal, bgfx::UniformHandle s_roughness,
    bgfx::TextureHandle fallback_albedo, bgfx::TextureHandle fallback_normal, bgfx::TextureHandle fallback_roughness,
    uint64_t state, const engine::ModelDrawParams& p)
{
    frame_.submit_world(view_id, program, s_albedo, s_normal, s_roughness,
        fallback_albedo, fallback_normal, fallback_roughness, state, p);

    engine::ModelDrawParams leaf_p = p;
    leaf_p.yaw = p.yaw + leaf_angle_;
    leaf_.submit_world(view_id, program, s_albedo, s_normal, s_roughness,
        fallback_albedo, fallback_normal, fallback_roughness, state, leaf_p);
}
