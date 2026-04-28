#include "audio.hpp"

#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

#include <SDL.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// Mix a small fixed pool of overlapping shot voices into the audio callback.
// Each call to play_pistol_shot() finds an inactive slot and starts it from frame 0;
// when all slots are busy, the oldest is restarted (always cuts the quietest tail).
constexpr int k_max_voices = 8;

struct Voice {
	std::atomic<bool> active{false};
	std::atomic<size_t> cursor_frames{0};
	std::atomic<uint64_t> start_seq{0};
};

SDL_AudioDeviceID g_dev = 0;
SDL_AudioSpec g_spec{};
std::vector<int16_t> g_shot_pcm;     // interleaved s16, channel-matched to device
size_t g_shot_frames = 0;            // length in frames (samples per channel)
int g_channels = 0;
std::array<Voice, k_max_voices> g_voices;
std::atomic<uint64_t> g_voice_seq{0};
bool g_audio_ok = false;

void audio_callback(void* /*userdata*/, Uint8* stream, int len)
{
	std::memset(stream, 0, static_cast<size_t>(len));
	if (g_shot_pcm.empty() || g_channels <= 0) {
		return;
	}

	int16_t* out = reinterpret_cast<int16_t*>(stream);
	const int total_samples = len / static_cast<int>(sizeof(int16_t));
	const int frames_to_fill = total_samples / g_channels;

	for (Voice& v : g_voices) {
		if (!v.active.load(std::memory_order_acquire)) {
			continue;
		}
		size_t cursor = v.cursor_frames.load(std::memory_order_relaxed);
		const size_t remaining = (cursor < g_shot_frames) ? (g_shot_frames - cursor) : 0;
		const size_t mix_frames = remaining < static_cast<size_t>(frames_to_fill)
			? remaining
			: static_cast<size_t>(frames_to_fill);

		const int16_t* src = g_shot_pcm.data() + cursor * static_cast<size_t>(g_channels);
		const size_t mix_samples = mix_frames * static_cast<size_t>(g_channels);
		for (size_t i = 0; i < mix_samples; ++i) {
			const int32_t mixed = static_cast<int32_t>(out[i]) + static_cast<int32_t>(src[i]);
			out[i] = static_cast<int16_t>(
				mixed > INT16_MAX ? INT16_MAX : (mixed < INT16_MIN ? INT16_MIN : mixed)
			);
		}

		cursor += mix_frames;
		if (cursor >= g_shot_frames) {
			v.active.store(false, std::memory_order_release);
		} else {
			v.cursor_frames.store(cursor, std::memory_order_relaxed);
		}
	}
}

void duplicate_mono_to_stereo(
	const int16_t* mono,
	size_t frames,
	std::vector<int16_t>& out
)
{
	out.resize(frames * 2);
	for (size_t i = 0; i < frames; ++i) {
		const int16_t s = mono[i];
		out[i * 2 + 0] = s;
		out[i * 2 + 1] = s;
	}
}

} // namespace

namespace engine {

bool audio_init(const char* pistol_shot_mp3_path, std::string& err)
{
	g_audio_ok = false;
	g_shot_pcm.clear();
	g_shot_frames = 0;
	g_channels = 0;

	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		err = SDL_GetError();
		return false;
	}

	drmp3_config cfg{};
	drmp3_uint64 frame_count = 0;
	drmp3_int16* pcm = drmp3_open_file_and_read_pcm_frames_s16(
		pistol_shot_mp3_path,
		&cfg,
		&frame_count,
		nullptr
	);
	if (!pcm || frame_count == 0) {
		err = "dr_mp3: failed to decode pistol shot";
		if (pcm) {
			drmp3_free(pcm, nullptr);
		}
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return false;
	}

	if (cfg.sampleRate == 0 || cfg.channels == 0 || cfg.channels > 2) {
		err = "dr_mp3: unsupported pistol shot audio format";
		drmp3_free(pcm, nullptr);
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return false;
	}

	const size_t src_frames = static_cast<size_t>(frame_count);
	int out_channels = static_cast<int>(cfg.channels);

	if (cfg.channels == 1) {
		std::vector<int16_t> stereo;
		duplicate_mono_to_stereo(reinterpret_cast<const int16_t*>(pcm), src_frames, stereo);
		g_shot_pcm = std::move(stereo);
		out_channels = 2;
	} else {
		g_shot_pcm.assign(
			reinterpret_cast<const int16_t*>(pcm),
			reinterpret_cast<const int16_t*>(pcm) + src_frames * cfg.channels
		);
	}
	g_shot_frames = src_frames;
	g_channels = out_channels;

	drmp3_free(pcm, nullptr);

	for (Voice& v : g_voices) {
		v.active.store(false, std::memory_order_relaxed);
		v.cursor_frames.store(0, std::memory_order_relaxed);
		v.start_seq.store(0, std::memory_order_relaxed);
	}

	SDL_AudioSpec want{};
	want.freq = static_cast<int>(cfg.sampleRate);
	want.format = AUDIO_S16SYS;
	want.channels = static_cast<Uint8>(out_channels);
	want.samples = 1024;
	want.callback = &audio_callback;
	want.userdata = nullptr;

	g_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &g_spec, 0);
	if (g_dev == 0) {
		err = SDL_GetError();
		g_shot_pcm.clear();
		g_shot_frames = 0;
		g_channels = 0;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return false;
	}

	if (g_spec.freq != want.freq
	    || g_spec.format != want.format
	    || g_spec.channels != want.channels) {
		err = "SDL audio device did not match decoded pistol shot format";
		SDL_CloseAudioDevice(g_dev);
		g_dev = 0;
		g_shot_pcm.clear();
		g_shot_frames = 0;
		g_channels = 0;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return false;
	}

	SDL_PauseAudioDevice(g_dev, 0);
	g_audio_ok = true;
	return true;
}

void audio_shutdown()
{
	if (g_dev != 0) {
		SDL_CloseAudioDevice(g_dev);
		g_dev = 0;
	}
	g_shot_pcm.clear();
	g_shot_frames = 0;
	g_channels = 0;
	if (g_audio_ok) {
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		g_audio_ok = false;
	}
}

void play_pistol_shot()
{
	if (g_dev == 0 || g_shot_pcm.empty()) {
		return;
	}

	const uint64_t seq = ++g_voice_seq;

	SDL_LockAudioDevice(g_dev);

	int chosen = -1;
	for (int i = 0; i < k_max_voices; ++i) {
		if (!g_voices[i].active.load(std::memory_order_relaxed)) {
			chosen = i;
			break;
		}
	}
	if (chosen < 0) {
		uint64_t oldest_seq = UINT64_MAX;
		for (int i = 0; i < k_max_voices; ++i) {
			const uint64_t s = g_voices[i].start_seq.load(std::memory_order_relaxed);
			if (s < oldest_seq) {
				oldest_seq = s;
				chosen = i;
			}
		}
	}

	if (chosen >= 0) {
		g_voices[chosen].cursor_frames.store(0, std::memory_order_relaxed);
		g_voices[chosen].start_seq.store(seq, std::memory_order_relaxed);
		g_voices[chosen].active.store(true, std::memory_order_release);
	}

	SDL_UnlockAudioDevice(g_dev);
}

} // namespace engine
