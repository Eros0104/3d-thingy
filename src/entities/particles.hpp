#pragma once

#include <bgfx/bgfx.h>
#include <vector>

namespace game {

class BloodParticleSystem {
public:
    void spawn(float x, float y, float z, int count);
    void update(float dt);
    void render(bgfx::ViewId view_id, bgfx::ProgramHandle prog) const;

private:
    struct Particle {
        float x, y, z;
        float vx, vy, vz;
        float life;
    };

    std::vector<Particle> particles_;
};

} // namespace game
