#include "game/animator.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>

namespace game {

namespace {

ViewmodelAnim classify_viewmodel(const char* name)
{
    if (!name) return ViewmodelAnim::Count;
    std::string s = name;
    auto has = [&](const char* needle) {
        std::string n = needle;
        return std::search(s.begin(), s.end(), n.begin(), n.end(),
            [](char a, char b){ return std::tolower(a) == std::tolower(b); }) != s.end();
    };
    if (has("idle"))                                return ViewmodelAnim::Idle;
    if (has("shoot") || has("fire"))                return ViewmodelAnim::Shoot;
    if (has("reload"))                              return ViewmodelAnim::Reload;
    if (has("take") || has("draw") || has("equip")) return ViewmodelAnim::Take;
    return ViewmodelAnim::Count;
}

ZombieAnim classify_zombie(const char* name)
{
    if (!name) return ZombieAnim::Count;
    std::string s = name;
    auto has = [&](const char* needle) {
        std::string n = needle;
        return std::search(s.begin(), s.end(), n.begin(), n.end(),
            [](char a, char b){ return std::tolower(a) == std::tolower(b); }) != s.end();
    };
    if (has("idle"))   return ZombieAnim::Idle;
    if (has("walk"))   return ZombieAnim::Walk;
    if (has("run"))    return ZombieAnim::Run;
    if (has("attack")) return ZombieAnim::Attack;
    if (has("death"))  return ZombieAnim::Death;
    if (has("hit"))    return ZombieAnim::GetHit;
    return ZombieAnim::Count;
}

} // namespace

void Animator::bind(const engine::RiggedModel& model)
{
    for (int i = 0; i < static_cast<int>(ViewmodelAnim::Count); ++i)
        vm_lookup_[i] = -1;
    for (int i = 0; i < static_cast<int>(ZombieAnim::Count); ++i)
        z_lookup_[i].clear();

    const int count = model.anim_count();
    for (int i = 0; i < count; ++i) {
        const char* name = model.anim_name(i);
        ViewmodelAnim vs = classify_viewmodel(name);
        if (vs != ViewmodelAnim::Count)
            vm_lookup_[static_cast<int>(vs)] = i;
        ZombieAnim zs = classify_zombie(name);
        if (zs != ZombieAnim::Count)
            z_lookup_[static_cast<int>(zs)].push_back(i);
    }
}

void Animator::play(engine::RiggedModel& m, ViewmodelAnim a, bool loop, bool restart_if_same)
{
    const int slot = static_cast<int>(a);
    if (slot < 0 || slot >= static_cast<int>(ViewmodelAnim::Count)) return;
    const int raw = vm_lookup_[slot];
    if (raw < 0) return;
    m.play_raw(raw, loop, restart_if_same, false);
}

void Animator::play(engine::RiggedModel& m, ZombieAnim a, bool loop, bool restart_if_same)
{
    const int slot = static_cast<int>(a);
    if (slot < 0 || slot >= static_cast<int>(ZombieAnim::Count)) return;
    const auto& variants = z_lookup_[slot];
    if (variants.empty()) return;
    m.play_raw(variants[0], loop, restart_if_same, true);
}

void Animator::play_random(engine::RiggedModel& m, ZombieAnim a, bool loop)
{
    const int slot = static_cast<int>(a);
    if (slot < 0 || slot >= static_cast<int>(ZombieAnim::Count)) return;
    const auto& variants = z_lookup_[slot];
    if (variants.empty()) return;
    const int raw = variants[static_cast<size_t>(std::rand()) % variants.size()];
    m.play_raw(raw, loop, true, true);
}

ViewmodelAnim Animator::current_vm(const engine::RiggedModel& m) const
{
    const int raw = m.current_raw();
    for (int i = 0; i < static_cast<int>(ViewmodelAnim::Count); ++i)
        if (vm_lookup_[i] == raw) return static_cast<ViewmodelAnim>(i);
    return ViewmodelAnim::Idle;
}

ZombieAnim Animator::current_z(const engine::RiggedModel& m) const
{
    const int raw = m.current_raw();
    for (int i = 0; i < static_cast<int>(ZombieAnim::Count); ++i)
        for (int v : z_lookup_[i])
            if (v == raw) return static_cast<ZombieAnim>(i);
    return ZombieAnim::Idle;
}

} // namespace game
