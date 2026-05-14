#pragma once

#include "render/rigged_model.hpp"

#include <vector>

namespace game {

enum class ViewmodelAnim : int {
    Idle   = 0,
    Shoot  = 1,
    Reload = 2,
    Take   = 3,
    Count  = 4,
};

enum class ZombieAnim : int {
    Idle   = 0,
    Walk   = 1,
    Run    = 2,
    Attack = 3,
    Death  = 4,
    GetHit = 5,
    Count  = 6,
};

// Maps game-level animation names to raw RiggedModel animation indices.
// Call bind() once after loading a model, then drive playback with play()/play_random().
struct Animator {
    void bind(const engine::RiggedModel& model);

    // No crossfade for first-person weapon animations.
    void play(engine::RiggedModel& m, ViewmodelAnim a, bool loop, bool restart_if_same);
    // Crossfades between zombie animations.
    void play(engine::RiggedModel& m, ZombieAnim   a, bool loop, bool restart_if_same);
    void play_random(engine::RiggedModel& m, ZombieAnim a, bool loop);

    ViewmodelAnim current_vm(const engine::RiggedModel& m) const;
    ZombieAnim    current_z (const engine::RiggedModel& m) const;

private:
    int              vm_lookup_[static_cast<int>(ViewmodelAnim::Count)] = {-1,-1,-1,-1};
    std::vector<int> z_lookup_ [static_cast<int>(ZombieAnim::Count)];
};

} // namespace game
