#pragma once

#include <string>

namespace engine {

using SoundId = int;
constexpr SoundId k_invalid_sound = -1;

bool audio_init(std::string &err);
void audio_shutdown();
SoundId audio_load(const char *path, std::string &err);
void audio_play(SoundId id);
void audio_set_looping(SoundId id, bool active);

} // namespace engine
