#include "entities/particles.hpp"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace game {

static constexpr float k_max_life    = 0.55f;
static constexpr float k_gravity     = -6.0f;
static constexpr float k_speed_min   = 1.5f;
static constexpr float k_speed_max   = 5.0f;
static constexpr uint32_t k_color    = 0xff2222ddu; // ABGR red

void BloodParticleSystem::spawn(float x, float y, float z, int count) {
    for (int i = 0; i < count; ++i) {
        const float azimuth  = (float(std::rand()) / RAND_MAX) * 6.2831853f;
        const float elev     = (float(std::rand()) / RAND_MAX) * bx::kPi; // hemisphere up
        const float speed    = k_speed_min + (float(std::rand()) / RAND_MAX) * (k_speed_max - k_speed_min);
        const float se       = std::sin(elev);
        Particle p;
        p.x  = x; p.y = y; p.z = z;
        p.vx = std::cos(azimuth) * se * speed;
        p.vy = std::cos(elev)    * speed;
        p.vz = std::sin(azimuth) * se * speed;
        p.life = k_max_life * (0.5f + 0.5f * float(std::rand()) / RAND_MAX);
        particles_.push_back(p);
    }
}

void BloodParticleSystem::update(float dt) {
    for (auto& p : particles_) {
        p.x  += p.vx * dt;
        p.y  += p.vy * dt;
        p.z  += p.vz * dt;
        p.vy += k_gravity * dt;
        p.life -= dt;
    }
    particles_.erase(
        std::remove_if(particles_.begin(), particles_.end(),
                       [](const Particle& p) { return p.life <= 0.f; }),
        particles_.end());
}

void BloodParticleSystem::render(bgfx::ViewId view_id, bgfx::ProgramHandle prog) const {
    if (particles_.empty() || !bgfx::isValid(prog))
        return;

    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Uint8, true)
        .end();

    // Two vertices per particle: tip (current pos) and tail (pos - vel * trail_len)
    constexpr float k_trail = 0.06f;
    const uint32_t n_verts = uint32_t(particles_.size()) * 2;
    if (bgfx::getAvailTransientVertexBuffer(n_verts, layout) < n_verts)
        return;

    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, n_verts, layout);

    struct V { float x, y, z; uint32_t abgr; };
    V* v = reinterpret_cast<V*>(tvb.data);
    for (uint32_t i = 0; i < uint32_t(particles_.size()); ++i) {
        const Particle& p = particles_[i];
        const float t     = p.life / k_max_life;
        const uint32_t col_tip  = 0xff0000ddu; // bright red tip (ABGR)
        const uint32_t col_tail = (uint32_t(t * 120.f) << 24) | 0x00000044u; // fading dark tail
        v[i*2+0] = { p.x,               p.y,               p.z,               col_tip  };
        v[i*2+1] = { p.x - p.vx*k_trail, p.y - p.vy*k_trail, p.z - p.vz*k_trail, col_tail };
    }

    float mtx[16];
    bx::mtxIdentity(mtx);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                   BGFX_STATE_PT_LINES   | BGFX_STATE_DEPTH_TEST_LESS);
    bgfx::setTransform(mtx);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::submit(view_id, prog);
}

} // namespace game
