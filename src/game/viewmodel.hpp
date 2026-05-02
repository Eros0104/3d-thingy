#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>
#include <string>

namespace engine {

enum class ViewmodelAnim : int {
	Idle = 0,
	Shoot = 1,
	Reload = 2,
	Take = 3,
	Count = 4,
};

enum class ZombieAnim : int {
	Idle = 0,
	Walk = 1,
	Run = 2,
	Attack = 3,
	Death = 4,
	GetHit = 5,
	Count = 6,
};

struct ViewmodelDrawParams {
	float eye[3];
	float yaw;
	float pitch;

	// Local offset in camera space: +X right, +Y up, +Z forward.
	float offset[3];
	// Extra euler tweaks applied in the model's local frame (radians).
	float tweak_pitch;
	float tweak_yaw;
	float tweak_roll;
	float scale;
};

struct CharacterDrawParams {
	float pos[3];   // world-space position
	float yaw;      // facing direction (radians, around Y axis)
	float scale;    // uniform scale
};

class Viewmodel {
public:
	Viewmodel();
	~Viewmodel();

	Viewmodel(const Viewmodel&) = delete;
	Viewmodel& operator=(const Viewmodel&) = delete;

	bool load(const char* glb_path, std::string& err);
	void unload();
	bool valid() const;

	void play(ViewmodelAnim anim, bool loop, bool restart_if_same);
	void play(ZombieAnim anim, bool loop, bool restart_if_same);
	bool current_anim_finished() const;
	ViewmodelAnim current_anim() const;

	void update(float dt);

	void submit(
		bgfx::ViewId view_id,
		bgfx::ProgramHandle program,
		bgfx::UniformHandle u_bones,
		bgfx::UniformHandle s_albedo,
		bgfx::UniformHandle u_baseColor,
		bgfx::TextureHandle fallback_white,
		uint64_t state,
		const ViewmodelDrawParams& params
	);

	void submit_world(
		bgfx::ViewId view_id,
		bgfx::ProgramHandle program,
		bgfx::UniformHandle u_bones,
		bgfx::UniformHandle s_albedo,
		bgfx::UniformHandle u_baseColor,
		bgfx::TextureHandle fallback_white,
		uint64_t state,
		const CharacterDrawParams& params
	);

	/// Submits a 3-line axis gizmo at the model anchor: red = X (pitch axis),
	/// green = Y (yaw axis), blue = Z (roll axis). Lines are `length` meters in world.
	void submit_axes_gizmo(
		bgfx::ViewId view_id,
		bgfx::ProgramHandle debug_program,
		const ViewmodelDrawParams& params,
		float length
	);

private:
	struct State;
	State* s_ = nullptr;
};

} // namespace engine
