#include "engine/collision_system.hpp"
#include "engine/physics/jolt_physics.hpp"
#include "game/fps_camera.hpp"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <cmath>

namespace {

void draw_capsule_wireframe(bgfx::ViewId view_id, bgfx::ProgramHandle prog,
                            float cx, float feet_y, float cz, float radius,
                            float height, uint32_t abgr) {
  if (!bgfx::isValid(prog))
    return;

  // N segments per full circle; N/2 per hemisphere arc.
  // Layout: 2 equatorial circles + 4 vertical shaft lines + 4 hemisphere arcs
  // (2 per end).
  constexpr int N = 16;
  constexpr int n_verts = N * 2 * 2 + 4 * 2 + 4 * (N / 2) * 2;

  bgfx::VertexLayout layout;
  layout.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .end();

  if (bgfx::getAvailTransientVertexBuffer(n_verts, layout) < n_verts)
    return;

  bgfx::TransientVertexBuffer tvb;
  bgfx::allocTransientVertexBuffer(&tvb, n_verts, layout);

  struct V {
    float x, y, z;
    uint32_t abgr;
  };
  V *v = reinterpret_cast<V *>(tvb.data);
  int idx = 0;

  const float bottom_y = feet_y + radius;
  const float top_y = feet_y + height - radius;
  constexpr float k_2pi = 6.28318530717958647692f;
  constexpr float k_pi = 3.14159265358979323846f;

  auto circle = [&](float ring_y) {
    for (int i = 0; i < N; ++i) {
      const float a0 = float(i) / N * k_2pi;
      const float a1 = float(i + 1) / N * k_2pi;
      v[idx++] = {cx + radius * std::cos(a0), ring_y,
                  cz + radius * std::sin(a0), abgr};
      v[idx++] = {cx + radius * std::cos(a1), ring_y,
                  cz + radius * std::sin(a1), abgr};
    }
  };
  circle(bottom_y);
  circle(top_y);

  const float offsets[4][2] = {
      {radius, 0}, {-radius, 0}, {0, radius}, {0, -radius}};
  for (auto &o : offsets) {
    v[idx++] = {cx + o[0], bottom_y, cz + o[1], abgr};
    v[idx++] = {cx + o[0], top_y, cz + o[1], abgr};
  }

  auto hemi_arc = [&](float center_y, float theta_start, float theta_end,
                      bool z_axis) {
    for (int i = 0; i < N / 2; ++i) {
      const float a0 =
          theta_start + (float(i)     / float(N / 2)) * (theta_end - theta_start);
      const float a1 =
          theta_start + (float(i + 1) / float(N / 2)) * (theta_end - theta_start);
      const float y0 = center_y + radius * std::sin(a0);
      const float y1 = center_y + radius * std::sin(a1);
      if (!z_axis) {
        v[idx++] = {cx + radius * std::cos(a0), y0, cz, abgr};
        v[idx++] = {cx + radius * std::cos(a1), y1, cz, abgr};
      } else {
        v[idx++] = {cx, y0, cz + radius * std::cos(a0), abgr};
        v[idx++] = {cx, y1, cz + radius * std::cos(a1), abgr};
      }
    }
  };
  hemi_arc(bottom_y, 0.f, -k_pi, false);
  hemi_arc(bottom_y, 0.f, -k_pi, true);
  hemi_arc(top_y, 0.f, k_pi, false);
  hemi_arc(top_y, 0.f, k_pi, true);

  float mtx[16];
  bx::mtxIdentity(mtx);
  bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                 BGFX_STATE_PT_LINES | BGFX_STATE_DEPTH_TEST_LESS);
  bgfx::setTransform(mtx);
  bgfx::setVertexBuffer(0, &tvb);
  bgfx::submit(view_id, prog);
}

} // namespace

namespace engine {

CollisionSystem::CollisionSystem(engine::JoltPhysics &jolt, bgfx::ProgramHandle debug_prog)
    : jolt_(&jolt), debug_prog_(debug_prog) {}

CollisionSystem::~CollisionSystem() = default;

void CollisionSystem::update() {
  for (auto z : zombies_) {
    if (!z->alive()) continue;

    FpsCamera p_cam = player_->camera();

    const float sep_dx   = z->x - p_cam.eyeX;
    // Push overlapping bodies apart (CharacterVirtual doesn't self-separate).
    const float sep_dz   = z->z - p_cam.eyeZ;
    const float sep_dist = std::sqrt(sep_dx * sep_dx + sep_dz * sep_dz);
    const float     k_min_sep         = z->k_collider.radius + player_->k_radius;

    if (sep_dist > 0.f && sep_dist < k_min_sep) {
        const float half_push = 0.5f * (k_min_sep - sep_dist) / sep_dist;

        z->x        += sep_dx * half_push;
        z->z        += sep_dz * half_push;
        p_cam.eyeX -= sep_dx * half_push;
        p_cam.eyeZ -= sep_dz * half_push;

        // Sync teleported positions back into Jolt.
        jolt_->set_character_pos(z->jolt_handle(), z->x, z->y, z->z);

        float px, py, pz;
        jolt_->get_character_pos(player_->jolt_handle(), px, py, pz);
        jolt_->set_character_pos(player_->jolt_handle(), p_cam.eyeX, py, p_cam.eyeZ);
    }
  }
}

void CollisionSystem::render(bgfx::ViewId view_id) {
  bgfx::ProgramHandle prog = debug_prog_;
  if (!debug_mode) return;

  const FpsCamera &cam = player_->camera();
  draw_capsule_wireframe(
      view_id, prog,
      cam.eyeX, cam.eyeY - game::Player::k_eye_height, cam.eyeZ,
      game::Player::k_radius, game::Player::k_eye_height,
      0xff44ff44u
  );

  for (const auto &z : zombies_) {
    const engine::Collider &zc = z->collider();
    draw_capsule_wireframe(
        view_id, prog, z->x, z->y, z->z, zc.radius, zc.height, 0xff4444ffu
    );
  }
}

void CollisionSystem::register_entity(game::Player* p) {
  player_ = p;
}

void CollisionSystem::register_entity(game::Zombie* z) {
  zombies_.push_back(z);
}

bool CollisionSystem::is_debug_mode() { return debug_mode; }

void CollisionSystem::toggle_debug_mode() { debug_mode = !debug_mode; }

} // namespace engine
