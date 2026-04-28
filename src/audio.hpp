#pragma once

#include <string>

namespace engine {

// `pistol_shot_mp3` is required and seeds the device sample rate / channel count.
// `footsteps_mp3` is optional — pass nullptr to skip; if loading fails, the rest of
// the audio system stays usable (a warning is printed to stderr).
bool audio_init(const char* pistol_shot_mp3, const char* footsteps_mp3, std::string& err);
void audio_shutdown();

void play_pistol_shot();

// Toggles the looping footsteps voice. Pauses/resumes from the current cursor so
// short stops don't restart the loop from frame 0.
void audio_set_walking(bool walking);

} // namespace engine
