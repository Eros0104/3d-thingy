#pragma once

#include <bgfx/bgfx.h>
#include <string>

namespace engine {

struct ModelDrawParams {
    float pos[3];
    float yaw;
    float scale;
};

class Model {
public:
    Model();
    ~Model();

    Model(const Model&)            = delete;
    Model& operator=(const Model&) = delete;

    bool load(const char* glb_path, std::string& err);
    void unload();
    bool valid() const;

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
        const ModelDrawParams& params
    );

private:
    struct State;
    State* s_ = nullptr;
};

} // namespace engine
