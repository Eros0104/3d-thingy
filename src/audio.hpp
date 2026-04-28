#pragma once

#include <string>

namespace engine {

bool audio_init(const char* pistol_shot_mp3_path, std::string& err);
void audio_shutdown();
void play_pistol_shot();

} // namespace engine
