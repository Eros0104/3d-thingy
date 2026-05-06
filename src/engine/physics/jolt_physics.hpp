#pragma once

#include <cstdint>
#include <memory>

namespace engine { struct LevelMeshOutput; }

namespace engine {

struct CharVerticalState { float velocity_y = 0.f; };

using CharHandle = uint32_t;
inline constexpr CharHandle k_invalid_char = UINT32_MAX;

struct JoltRayHit {
    float t  = 0.f;
    float nx = 0.f, ny = 0.f, nz = 0.f;
};

class JoltPhysics {
public:
    JoltPhysics();
    ~JoltPhysics();
    JoltPhysics(const JoltPhysics&)            = delete;
    JoltPhysics& operator=(const JoltPhysics&) = delete;

    bool init();
    void shutdown();

    // Build static level collision from CPU mesh. Call once after level load.
    void build_level(const LevelMeshOutput& mesh);

    // Add capsule character (height = total from feet to head). Returns k_invalid_char on failure.
    CharHandle add_character(float x, float y, float z,
                             float radius, float height, float step_height);
    void remove_character(CharHandle h);

    // Move character with desired horizontal velocity (m/s). Updates vertical state with gravity.
    void move_character(CharHandle h, CharVerticalState& vs,
                        float vel_x, float vel_z, float dt);

    // Read/write character foot position (world Y = feet).
    void get_character_pos(CharHandle h, float& x, float& y, float& z) const;
    void set_character_pos(CharHandle h, float x, float y, float z);

    // Cast a ray against level geometry only. Returns false if nothing hit.
    bool cast_ray_level(float ox, float oy, float oz,
                        float dx, float dy, float dz,
                        float max_dist,
                        JoltRayHit& hit) const;

    // Step the physics world (call once per frame before character updates).
    void update(float dt);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine
