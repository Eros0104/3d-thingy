#include "audio.hpp"

#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

#include <SDL.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

// One shared voice pool: most slots are one-shot pistol voices that pick the next
// free slot (or steal the oldest); one dedicated slot loops the footsteps clip.
constexpr int k_max_voices = 9;
constexpr int k_steps_voice_index = k_max_voices - 1;

struct Clip {
	std::vector<int16_t> pcm;  // interleaved s16, channel-matched to device
	size_t frames = 0;
};

struct Voice {
	std::atomic<bool> active{false};
	std::atomic<bool> loop{false};
	std::atomic<size_t> cursor_frames{0};
	std::atomic<uint64_t> start_seq{0};
	Clip* clip = nullptr;  // mutated only under SDL_LockAudioDevice
};

SDL_AudioDeviceID g_dev = 0;
SDL_AudioSpec g_spec{};
int g_channels = 0;

Clip g_shot;
Clip g_steps;
bool g_steps_loaded = false;

std::array<Voice, k_max_voices> g_voices;
std::atomic<uint64_t> g_voice_seq{0};
bool g_audio_ok = false;

inline int16_t saturate_s16(int32_t v)
{
	if (v > INT16_MAX) return INT16_MAX;
	if (v < INT16_MIN) return INT16_MIN;
	return static_cast<int16_t>(v);
}

void audio_callback(void* /*userdata*/, Uint8* stream, int len)
{
	std::memset(stream, 0, static_cast<size_t>(len));
	if (g_channels <= 0) {
		return;
	}

	int16_t* out = reinterpret_cast<int16_t*>(stream);
	const int total_samples = len / static_cast<int>(sizeof(int16_t));
	const size_t frames_to_fill = static_cast<size_t>(total_samples / g_channels);

	for (Voice& v : g_voices) {
		if (!v.active.load(std::memory_order_acquire)) {
			continue;
		}
		Clip* c = v.clip;
		if (!c || c->frames == 0) {
			v.active.store(false, std::memory_order_release);
			continue;
		}

		size_t cursor = v.cursor_frames.load(std::memory_order_relaxed);
		const bool loop = v.loop.load(std::memory_order_relaxed);
		size_t needed = frames_to_fill;
		size_t out_off_frames = 0;

		while (needed > 0) {
			if (cursor >= c->frames) {
				if (!loop) {
					break;
				}
				cursor = 0;
			}
			const size_t available = c->frames - cursor;
			const size_t n = available < needed ? available : needed;

			const int16_t* src = c->pcm.data() + cursor * static_cast<size_t>(g_channels);
			int16_t* dst = out + out_off_frames * static_cast<size_t>(g_channels);
			const size_t mix_samples = n * static_cast<size_t>(g_channels);
			for (size_t i = 0; i < mix_samples; ++i) {
				dst[i] = saturate_s16(static_cast<int32_t>(dst[i]) + static_cast<int32_t>(src[i]));
			}
			cursor += n;
			out_off_frames += n;
			needed -= n;
		}

		if (cursor >= c->frames && !loop) {
			v.active.store(false, std::memory_order_release);
			v.cursor_frames.store(0, std::memory_order_relaxed);
		} else {
			v.cursor_frames.store(cursor, std::memory_order_relaxed);
		}
	}
}

void duplicate_mono_to_stereo(const int16_t* mono, size_t frames, std::vector<int16_t>& out)
{
	out.resize(frames * 2);
	for (size_t i = 0; i < frames; ++i) {
		const int16_t s = mono[i];
		out[i * 2 + 0] = s;
		out[i * 2 + 1] = s;
	}
}

bool decode_mp3(const char* path, drmp3_int16*& pcm, drmp3_uint64& frames, drmp3_uint32& rate, drmp3_uint32& channels)
{
	drmp3_config cfg{};
	pcm = drmp3_open_file_and_read_pcm_frames_s16(path, &cfg, &frames, nullptr);
	if (!pcm || frames == 0 || cfg.sampleRate == 0 || cfg.channels == 0 || cfg.channels > 2) {
		if (pcm) {
			drmp3_free(pcm, nullptr);
			pcm = nullptr;
		}
		return false;
	}
	rate = cfg.sampleRate;
	channels = cfg.channels;
	return true;
}

// Resamples / channel-converts a decoded MP3 to the device format using SDL_BuildAudioCVT.
bool convert_to_device(
	const int16_t* src,
	size_t src_frames,
	int src_channels,
	int src_rate,
	int dst_channels,
	int dst_rate,
	std::vector<int16_t>& out_pcm,
	size_t& out_frames
)
{
	if (src_channels == dst_channels && src_rate == dst_rate) {
		out_pcm.assign(src, src + src_frames * static_cast<size_t>(src_channels));
		out_frames = src_frames;
		return true;
	}

	SDL_AudioCVT cvt;
	const int cvt_rv = SDL_BuildAudioCVT(
		&cvt,
		AUDIO_S16SYS, static_cast<Uint8>(src_channels), src_rate,
		AUDIO_S16SYS, static_cast<Uint8>(dst_channels), dst_rate
	);
	if (cvt_rv < 0) {
		return false;
	}
	if (cvt_rv == 0) {
		out_pcm.assign(src, src + src_frames * static_cast<size_t>(src_channels));
		out_frames = src_frames;
		return true;
	}

	const int src_bytes = static_cast<int>(src_frames) * src_channels * static_cast<int>(sizeof(int16_t));
	cvt.len = src_bytes;
	std::vector<Uint8> buf(static_cast<size_t>(src_bytes) * static_cast<size_t>(cvt.len_mult));
	std::memcpy(buf.data(), src, static_cast<size_t>(src_bytes));
	cvt.buf = buf.data();
	if (SDL_ConvertAudio(&cvt) != 0) {
		return false;
	}

	const size_t out_bytes = static_cast<size_t>(cvt.len_cvt);
	const size_t bytes_per_frame = static_cast<size_t>(dst_channels) * sizeof(int16_t);
	out_frames = out_bytes / bytes_per_frame;
	out_pcm.assign(
		reinterpret_cast<const int16_t*>(buf.data()),
		reinterpret_cast<const int16_t*>(buf.data() + out_frames * bytes_per_frame)
	);
	return true;
}

void reset_voices()
{
	for (Voice& v : g_voices) {
		v.active.store(false, std::memory_order_relaxed);
		v.loop.store(false, std::memory_order_relaxed);
		v.cursor_frames.store(0, std::memory_order_relaxed);
		v.start_seq.store(0, std::memory_order_relaxed);
		v.clip = nullptr;
	}
}

bool load_footsteps_clip(const char* path, int dst_channels, int dst_rate)
{
	drmp3_int16* src_pcm = nullptr;
	drmp3_uint64 src_frames = 0;
	drmp3_uint32 src_rate = 0;
	drmp3_uint32 src_channels = 0;
	if (!decode_mp3(path, src_pcm, src_frames, src_rate, src_channels)) {
		std::fprintf(stderr, "audio: failed to decode footsteps mp3 \"%s\"\n", path);
		return false;
	}

	std::vector<int16_t> staged;
	const int16_t* src = src_pcm;

	if (src_channels == 1 && dst_channels == 2) {
		duplicate_mono_to_stereo(src_pcm, static_cast<size_t>(src_frames), staged);
		src = staged.data();
		src_channels = 2;
	}

	std::vector<int16_t> converted;
	size_t conv_frames = 0;
	if (!convert_to_device(
		src,
		static_cast<size_t>(src_frames),
		static_cast<int>(src_channels),
		static_cast<int>(src_rate),
		dst_channels,
		dst_rate,
		converted,
		conv_frames
	)) {
		std::fprintf(stderr, "audio: failed to convert footsteps to device format\n");
		drmp3_free(src_pcm, nullptr);
		return false;
	}

	drmp3_free(src_pcm, nullptr);

	g_steps.pcm = std::move(converted);
	g_steps.frames = conv_frames;
	return true;
}

} // namespace

namespace engine {

bool audio_init(const char* pistol_shot_mp3, const char* footsteps_mp3, std::string& err)
{
	g_audio_ok = false;
	g_shot = {};
	g_steps = {};
	g_steps_loaded = false;
	g_channels = 0;
	reset_voices();

	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		err = SDL_GetError();
		return false;
	}

	drmp3_int16* shot_pcm = nullptr;
	drmp3_uint64 shot_frames = 0;
	drmp3_uint32 shot_rate = 0;
	drmp3_uint32 shot_channels = 0;
	if (!decode_mp3(pistol_shot_mp3, shot_pcm, shot_frames, shot_rate, shot_channels)) {
		err = "dr_mp3: failed to decode pistol shot";
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return false;
	}

	int out_channels = static_cast<int>(shot_channels);
	if (shot_channels == 1) {
		std::vector<int16_t> stereo;
		duplicate_mono_to_stereo(shot_pcm, static_cast<size_t>(shot_frames), stereo);
		g_shot.pcm = std::move(stereo);
		out_channels = 2;
	} else {
		g_shot.pcm.assign(
			reinterpret_cast<const int16_t*>(shot_pcm),
			reinterpret_cast<const int16_t*>(shot_pcm) + shot_frames * shot_channels
		);
	}
	g_shot.frames = static_cast<size_t>(shot_frames);
	g_channels = out_channels;
	drmp3_free(shot_pcm, nullptr);

	SDL_AudioSpec want{};
	want.freq = static_cast<int>(shot_rate);
	want.format = AUDIO_S16SYS;
	want.channels = static_cast<Uint8>(out_channels);
	want.samples = 1024;
	want.callback = &audio_callback;
	want.userdata = nullptr;

	g_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &g_spec, 0);
	if (g_dev == 0) {
		err = SDL_GetError();
		g_shot = {};
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
		g_shot = {};
		g_channels = 0;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return false;
	}

	if (footsteps_mp3 != nullptr) {
		g_steps_loaded = load_footsteps_clip(footsteps_mp3, out_channels, g_spec.freq);
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
	g_shot = {};
	g_steps = {};
	g_steps_loaded = false;
	g_channels = 0;
	reset_voices();
	if (g_audio_ok) {
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		g_audio_ok = false;
	}
}

void play_pistol_shot()
{
	if (g_dev == 0 || g_shot.frames == 0) {
		return;
	}

	const uint64_t seq = ++g_voice_seq;

	SDL_LockAudioDevice(g_dev);

	int chosen = -1;
	for (int i = 0; i < k_steps_voice_index; ++i) {
		if (!g_voices[i].active.load(std::memory_order_relaxed)) {
			chosen = i;
			break;
		}
	}
	if (chosen < 0) {
		uint64_t oldest_seq = UINT64_MAX;
		for (int i = 0; i < k_steps_voice_index; ++i) {
			const uint64_t s = g_voices[i].start_seq.load(std::memory_order_relaxed);
			if (s < oldest_seq) {
				oldest_seq = s;
				chosen = i;
			}
		}
	}

	if (chosen >= 0) {
		g_voices[chosen].clip = &g_shot;
		g_voices[chosen].loop.store(false, std::memory_order_relaxed);
		g_voices[chosen].cursor_frames.store(0, std::memory_order_relaxed);
		g_voices[chosen].start_seq.store(seq, std::memory_order_relaxed);
		g_voices[chosen].active.store(true, std::memory_order_release);
	}

	SDL_UnlockAudioDevice(g_dev);
}

void audio_set_walking(bool walking)
{
	if (g_dev == 0 || !g_steps_loaded || g_steps.frames == 0) {
		return;
	}

	Voice& v = g_voices[k_steps_voice_index];
	const bool currently_active = v.active.load(std::memory_order_relaxed);
	if (walking == currently_active) {
		return;
	}

	SDL_LockAudioDevice(g_dev);
	if (walking) {
		v.clip = &g_steps;
		v.loop.store(true, std::memory_order_relaxed);
		v.active.store(true, std::memory_order_release);
	} else {
		v.active.store(false, std::memory_order_release);
	}
	SDL_UnlockAudioDevice(g_dev);
}

} // namespace engine
