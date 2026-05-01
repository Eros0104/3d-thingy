#include "engine/audio.hpp"

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

// One shared voice pool: most slots are one-shot voices that pick the next
// free slot (or steal the oldest); one dedicated slot loops a looping clip.
constexpr int k_max_voices = 9;
constexpr int k_loop_voice_index = k_max_voices - 1;

constexpr int k_device_channels = 2;
constexpr int k_device_rate = 44100;

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

std::vector<Clip> g_clips;
int g_loop_sound_id = engine::k_invalid_sound;

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

	int16_t* out = reinterpret_cast<int16_t*>(stream);
	const int total_samples = len / static_cast<int>(sizeof(int16_t));
	const size_t frames_to_fill = static_cast<size_t>(total_samples / k_device_channels);

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

			const int16_t* src = c->pcm.data() + cursor * static_cast<size_t>(k_device_channels);
			int16_t* dst = out + out_off_frames * static_cast<size_t>(k_device_channels);
			const size_t mix_samples = n * static_cast<size_t>(k_device_channels);
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

} // namespace

namespace engine {

bool audio_init(std::string& err)
{
	g_audio_ok = false;
	g_clips.clear();
	g_loop_sound_id = k_invalid_sound;
	reset_voices();

	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		err = SDL_GetError();
		return false;
	}

	SDL_AudioSpec want{};
	want.freq = k_device_rate;
	want.format = AUDIO_S16SYS;
	want.channels = static_cast<Uint8>(k_device_channels);
	want.samples = 1024;
	want.callback = &audio_callback;
	want.userdata = nullptr;

	g_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &g_spec, 0);
	if (g_dev == 0) {
		err = SDL_GetError();
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
	g_clips.clear();
	g_loop_sound_id = k_invalid_sound;
	reset_voices();
	if (g_audio_ok) {
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		g_audio_ok = false;
	}
}

SoundId audio_load(const char* path, std::string& err)
{
	if (!g_audio_ok) {
		err = "audio not initialized";
		return k_invalid_sound;
	}

	drmp3_int16* src_pcm = nullptr;
	drmp3_uint64 src_frames = 0;
	drmp3_uint32 src_rate = 0;
	drmp3_uint32 src_channels = 0;
	if (!decode_mp3(path, src_pcm, src_frames, src_rate, src_channels)) {
		err = std::string("dr_mp3: failed to decode \"") + path + "\"";
		return k_invalid_sound;
	}

	std::vector<int16_t> staged;
	const int16_t* src = src_pcm;
	int channels_after_upconv = static_cast<int>(src_channels);

	if (src_channels == 1 && k_device_channels == 2) {
		duplicate_mono_to_stereo(src_pcm, static_cast<size_t>(src_frames), staged);
		src = staged.data();
		channels_after_upconv = 2;
	}

	Clip clip;
	size_t conv_frames = 0;
	if (!convert_to_device(
		src,
		static_cast<size_t>(src_frames),
		channels_after_upconv,
		static_cast<int>(src_rate),
		k_device_channels,
		k_device_rate,
		clip.pcm,
		conv_frames
	)) {
		drmp3_free(src_pcm, nullptr);
		err = std::string("audio: failed to convert \"") + path + "\" to device format";
		return k_invalid_sound;
	}

	drmp3_free(src_pcm, nullptr);
	clip.frames = conv_frames;
	g_clips.push_back(std::move(clip));
	return static_cast<SoundId>(g_clips.size() - 1);
}

void audio_play(SoundId id)
{
	if (id == k_invalid_sound || id < 0 || id >= static_cast<int>(g_clips.size())) {
		return;
	}
	if (g_dev == 0 || g_clips[static_cast<size_t>(id)].frames == 0) {
		return;
	}

	const uint64_t seq = ++g_voice_seq;

	SDL_LockAudioDevice(g_dev);

	int chosen = -1;
	for (int i = 0; i < k_loop_voice_index; ++i) {
		if (!g_voices[i].active.load(std::memory_order_relaxed)) {
			chosen = i;
			break;
		}
	}
	if (chosen < 0) {
		uint64_t oldest_seq = UINT64_MAX;
		for (int i = 0; i < k_loop_voice_index; ++i) {
			const uint64_t s = g_voices[i].start_seq.load(std::memory_order_relaxed);
			if (s < oldest_seq) {
				oldest_seq = s;
				chosen = i;
			}
		}
	}

	if (chosen >= 0) {
		g_voices[chosen].clip = &g_clips[static_cast<size_t>(id)];
		g_voices[chosen].loop.store(false, std::memory_order_relaxed);
		g_voices[chosen].cursor_frames.store(0, std::memory_order_relaxed);
		g_voices[chosen].start_seq.store(seq, std::memory_order_relaxed);
		g_voices[chosen].active.store(true, std::memory_order_release);
	}

	SDL_UnlockAudioDevice(g_dev);
}

void audio_set_looping(SoundId id, bool active)
{
	if (g_dev == 0) {
		return;
	}

	Voice& v = g_voices[k_loop_voice_index];

	if (!active) {
		// Stop the loop voice regardless of id.
		SDL_LockAudioDevice(g_dev);
		v.active.store(false, std::memory_order_release);
		SDL_UnlockAudioDevice(g_dev);
		return;
	}

	if (id == k_invalid_sound || id < 0 || id >= static_cast<int>(g_clips.size())) {
		return;
	}
	if (g_clips[static_cast<size_t>(id)].frames == 0) {
		return;
	}

	// If already looping the same sound and it's active, return early.
	if (g_loop_sound_id == id && v.active.load(std::memory_order_relaxed)) {
		return;
	}

	g_loop_sound_id = id;
	SDL_LockAudioDevice(g_dev);
	v.clip = &g_clips[static_cast<size_t>(id)];
	v.loop.store(true, std::memory_order_relaxed);
	v.active.store(true, std::memory_order_release);
	SDL_UnlockAudioDevice(g_dev);
}

} // namespace engine
