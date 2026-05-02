#include "game/viewmodel.hpp"
#include "engine/skinned_vertex.hpp"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

namespace {

constexpr int k_max_bones = 120;

struct LocalTRS {
	float t[3] = {0.0f, 0.0f, 0.0f};
	float r[4] = {0.0f, 0.0f, 0.0f, 1.0f};
	float s[3] = {1.0f, 1.0f, 1.0f};
};

struct NodeData {
	int parent = -1;
	LocalTRS rest;
	LocalTRS current;
	float world[16];
};

enum class AnimPath : uint8_t { Translation = 0, Rotation = 1, Scale = 2 };

struct AnimSampler {
	std::vector<float> input;
	std::vector<float> output;
	int components = 0;
	int interp = 0; // 0 LINEAR, 1 STEP, 2 CUBICSPLINE
};

struct AnimChannel {
	int node = -1;
	AnimPath path;
	int sampler = -1;
};

struct Animation {
	std::string name;
	std::vector<AnimChannel> channels;
	std::vector<AnimSampler> samplers;
	float duration = 0.0f;
};

struct Primitive {
	bgfx::VertexBufferHandle vb = BGFX_INVALID_HANDLE;
	bgfx::IndexBufferHandle ib = BGFX_INVALID_HANDLE;
	bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
	float base_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

// bx::mtxMul(out, a, b) computes out = b * a (NOT a * b in math notation).
// We use math notation locally — out = A * B.
inline void mat_mul(float out[16], const float a[16], const float b[16])
{
	bx::mtxMul(out, b, a);
}

void quat_to_mtx(float m[16], const float q[4])
{
	const float x = q[0], y = q[1], z = q[2], w = q[3];
	m[0]  = 1.0f - 2.0f * y * y - 2.0f * z * z;
	m[1]  = 2.0f * x * y + 2.0f * z * w;
	m[2]  = 2.0f * x * z - 2.0f * y * w;
	m[3]  = 0.0f;
	m[4]  = 2.0f * x * y - 2.0f * z * w;
	m[5]  = 1.0f - 2.0f * x * x - 2.0f * z * z;
	m[6]  = 2.0f * y * z + 2.0f * x * w;
	m[7]  = 0.0f;
	m[8]  = 2.0f * x * z + 2.0f * y * w;
	m[9]  = 2.0f * y * z - 2.0f * x * w;
	m[10] = 1.0f - 2.0f * x * x - 2.0f * y * y;
	m[11] = 0.0f;
	m[12] = 0.0f;
	m[13] = 0.0f;
	m[14] = 0.0f;
	m[15] = 1.0f;
}

void compute_local_matrix(const LocalTRS& trs, float out[16])
{
	float r[16];
	quat_to_mtx(r, trs.r);
	out[0]  = r[0]  * trs.s[0];
	out[1]  = r[1]  * trs.s[0];
	out[2]  = r[2]  * trs.s[0];
	out[3]  = 0.0f;
	out[4]  = r[4]  * trs.s[1];
	out[5]  = r[5]  * trs.s[1];
	out[6]  = r[6]  * trs.s[1];
	out[7]  = 0.0f;
	out[8]  = r[8]  * trs.s[2];
	out[9]  = r[9]  * trs.s[2];
	out[10] = r[10] * trs.s[2];
	out[11] = 0.0f;
	out[12] = trs.t[0];
	out[13] = trs.t[1];
	out[14] = trs.t[2];
	out[15] = 1.0f;
}

void quat_normalize(float q[4])
{
	float n2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
	if (n2 > 1e-12f) {
		float inv = 1.0f / std::sqrt(n2);
		q[0] *= inv;
		q[1] *= inv;
		q[2] *= inv;
		q[3] *= inv;
	} else {
		q[0] = 0.0f; q[1] = 0.0f; q[2] = 0.0f; q[3] = 1.0f;
	}
}

void quat_slerp(const float a[4], const float b[4], float t, float out[4])
{
	float bn[4] = {b[0], b[1], b[2], b[3]};
	float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
	if (dot < 0.0f) {
		bn[0] = -b[0]; bn[1] = -b[1]; bn[2] = -b[2]; bn[3] = -b[3];
		dot = -dot;
	}
	if (dot > 0.9995f) {
		out[0] = a[0] + t * (bn[0] - a[0]);
		out[1] = a[1] + t * (bn[1] - a[1]);
		out[2] = a[2] + t * (bn[2] - a[2]);
		out[3] = a[3] + t * (bn[3] - a[3]);
		quat_normalize(out);
		return;
	}
	float theta_0 = std::acos(dot);
	float theta = theta_0 * t;
	float sin_t = std::sin(theta);
	float sin_t0 = std::sin(theta_0);
	float s0 = std::cos(theta) - dot * sin_t / sin_t0;
	float s1 = sin_t / sin_t0;
	out[0] = s0 * a[0] + s1 * bn[0];
	out[1] = s0 * a[1] + s1 * bn[1];
	out[2] = s0 * a[2] + s1 * bn[2];
	out[3] = s0 * a[3] + s1 * bn[3];
}

int find_keyframe(const std::vector<float>& input, float t, float& alpha)
{
	if (input.empty()) {
		alpha = 0.0f;
		return 0;
	}
	if (t <= input.front()) {
		alpha = 0.0f;
		return 0;
	}
	if (t >= input.back()) {
		alpha = 0.0f;
		return static_cast<int>(input.size()) - 1;
	}
	// Binary search for largest i with input[i] <= t.
	size_t lo = 0;
	size_t hi = input.size() - 1;
	while (lo + 1 < hi) {
		size_t mid = (lo + hi) / 2;
		if (input[mid] <= t) {
			lo = mid;
		} else {
			hi = mid;
		}
	}
	float dt = input[lo + 1] - input[lo];
	alpha = dt > 1e-6f ? (t - input[lo]) / dt : 0.0f;
	return static_cast<int>(lo);
}

bgfx::TextureHandle decode_texture_from_memory(const uint8_t* data, size_t size, uint64_t flags)
{
	if (!data || size == 0) {
		return BGFX_INVALID_HANDLE;
	}
	bx::DefaultAllocator alloc;
	bimg::ImageContainer* img = bimg::imageParse(&alloc, data, static_cast<uint32_t>(size));
	if (!img) {
		std::fprintf(stderr, "  tex: bimg::imageParse failed (size=%zu, magic=0x%02x%02x%02x%02x)\n",
			size, data[0], data[1], data[2], data[3]);
		return BGFX_INVALID_HANDLE;
	}
	std::fprintf(stderr, "  tex: bimg %dx%d fmt=%d mips=%d layers=%d\n",
		img->m_width, img->m_height, static_cast<int>(img->m_format),
		static_cast<int>(img->m_numMips), static_cast<int>(img->m_numLayers));
	if (img->m_cubeMap || img->m_depth > 1) {
		bimg::imageFree(img);
		return BGFX_INVALID_HANDLE;
	}
	const bgfx::Memory* mem = bgfx::copy(img->m_data, img->m_size);
	bgfx::TextureHandle h = bgfx::createTexture2D(
		static_cast<uint16_t>(img->m_width),
		static_cast<uint16_t>(img->m_height),
		img->m_numMips > 1,
		img->m_numLayers,
		static_cast<bgfx::TextureFormat::Enum>(img->m_format),
		flags,
		mem
	);
	std::fprintf(stderr, "  tex: createTexture2D -> handle=%d\n", h.idx);
	bimg::imageFree(img);
	return h;
}

ViewmodelAnim classify_anim_name(const char* name)
{
	if (!name) return ViewmodelAnim::Count;
	std::string s = name;
	auto contains = [&](const char* needle) {
		std::string n = needle;
		auto it = std::search(
			s.begin(), s.end(), n.begin(), n.end(),
			[](char a, char b) { return std::tolower(a) == std::tolower(b); }
		);
		return it != s.end();
	};
	if (contains("idle"))   return ViewmodelAnim::Idle;
	if (contains("shoot") || contains("fire")) return ViewmodelAnim::Shoot;
	if (contains("reload")) return ViewmodelAnim::Reload;
	if (contains("take") || contains("draw") || contains("equip")) return ViewmodelAnim::Take;
	return ViewmodelAnim::Count;
}

ZombieAnim classify_zombie_anim_name(const char* name)
{
	if (!name) return ZombieAnim::Count;
	std::string s = name;
	auto contains = [&](const char* needle) {
		std::string n = needle;
		auto it = std::search(
			s.begin(), s.end(), n.begin(), n.end(),
			[](char a, char b) { return std::tolower(a) == std::tolower(b); }
		);
		return it != s.end();
	};
	if (contains("idle"))   return ZombieAnim::Idle;
	if (contains("walk"))   return ZombieAnim::Walk;
	if (contains("run"))    return ZombieAnim::Run;
	if (contains("attack")) return ZombieAnim::Attack;
	if (contains("death"))  return ZombieAnim::Death;
	if (contains("hit"))    return ZombieAnim::GetHit;
	return ZombieAnim::Count;
}

} // namespace

struct Viewmodel::State {
	bgfx::VertexLayout layout;
	std::vector<NodeData> nodes;
	std::vector<int> joint_nodes;
	std::vector<float> inv_bind;
	std::vector<int> traversal;
	std::vector<Primitive> primitives;
	std::vector<bgfx::TextureHandle> owned_textures;
	std::vector<Animation> animations;
	int anim_lookup[static_cast<int>(ViewmodelAnim::Count)] = {-1, -1, -1, -1};
	int zombie_anim_lookup[static_cast<int>(ZombieAnim::Count)] = {-1, -1, -1, -1, -1, -1};

	int current = -1; // raw index into animations[], -1 = none
	float time = 0.0f;
	bool looping = true;
	bool finished = false;

	std::vector<float> bone_matrices;
	bool loaded = false;
};

Viewmodel::Viewmodel() : s_(new State())
{
	s_->bone_matrices.assign(static_cast<size_t>(k_max_bones) * 16, 0.0f);
	for (int i = 0; i < k_max_bones; ++i) {
		bx::mtxIdentity(&s_->bone_matrices[static_cast<size_t>(i) * 16]);
	}
	s_->layout.begin()
		.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
		.add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
		.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
		// Metal rejects int4 reading from MTLAttributeFormatUChar4. Use normalized=true so
		// uchar gets converted to a 0..1 float; rescale to 0..255 in the shader.
		.add(bgfx::Attrib::Indices, 4, bgfx::AttribType::Uint8, true, false)
		.add(bgfx::Attrib::Weight, 4, bgfx::AttribType::Float)
		.end();
}

Viewmodel::~Viewmodel()
{
	unload();
	delete s_;
	s_ = nullptr;
}

void Viewmodel::unload()
{
	for (auto& p : s_->primitives) {
		if (bgfx::isValid(p.vb)) bgfx::destroy(p.vb);
		if (bgfx::isValid(p.ib)) bgfx::destroy(p.ib);
	}
	for (auto& t : s_->owned_textures) {
		if (bgfx::isValid(t)) bgfx::destroy(t);
	}
	s_->primitives.clear();
	s_->owned_textures.clear();
	s_->nodes.clear();
	s_->joint_nodes.clear();
	s_->inv_bind.clear();
	s_->traversal.clear();
	s_->animations.clear();
	for (int i = 0; i < static_cast<int>(ViewmodelAnim::Count); ++i) s_->anim_lookup[i] = -1;
	for (int i = 0; i < static_cast<int>(ZombieAnim::Count); ++i) s_->zombie_anim_lookup[i] = -1;
	s_->current = -1;
	s_->loaded = false;
}

bool Viewmodel::valid() const { return s_->loaded; }
ViewmodelAnim Viewmodel::current_anim() const {
	for (int i = 0; i < static_cast<int>(ViewmodelAnim::Count); ++i) {
		if (s_->anim_lookup[i] == s_->current) return static_cast<ViewmodelAnim>(i);
	}
	return ViewmodelAnim::Idle;
}
bool Viewmodel::current_anim_finished() const { return s_->finished; }

void Viewmodel::play(ViewmodelAnim anim, bool loop, bool restart_if_same)
{
	int slot = static_cast<int>(anim);
	if (slot < 0 || slot >= static_cast<int>(ViewmodelAnim::Count)) return;
	int raw = s_->anim_lookup[slot];
	if (raw < 0) return;
	if (raw == s_->current && !restart_if_same) {
		s_->looping = loop;
		return;
	}
	s_->current = raw;
	s_->time = 0.0f;
	s_->looping = loop;
	s_->finished = false;
}

void Viewmodel::play(ZombieAnim anim, bool loop, bool restart_if_same)
{
	int slot = static_cast<int>(anim);
	if (slot < 0 || slot >= static_cast<int>(ZombieAnim::Count)) return;
	int raw = s_->zombie_anim_lookup[slot];
	if (raw < 0) return;
	if (raw == s_->current && !restart_if_same) {
		s_->looping = loop;
		return;
	}
	s_->current = raw;
	s_->time = 0.0f;
	s_->looping = loop;
	s_->finished = false;
}

bool Viewmodel::load(const char* glb_path, std::string& err)
{
	cgltf_options opts{};
	cgltf_data* data = nullptr;
	cgltf_result r = cgltf_parse_file(&opts, glb_path, &data);
	if (r != cgltf_result_success) {
		err = "cgltf_parse_file failed";
		return false;
	}
	r = cgltf_load_buffers(&opts, data, glb_path);
	if (r != cgltf_result_success) {
		cgltf_free(data);
		err = "cgltf_load_buffers failed";
		return false;
	}

	const size_t node_count = data->nodes_count;
	s_->nodes.resize(node_count);

	for (size_t i = 0; i < node_count; ++i) {
		const cgltf_node& n = data->nodes[i];
		NodeData& nd = s_->nodes[i];
		nd.parent = n.parent ? static_cast<int>(n.parent - data->nodes) : -1;
		LocalTRS trs;
		if (n.has_matrix) {
			// Decompose by reading translation/rotation/scale from cgltf helper-like logic.
			// Simple decomposition: extract translation from m[12..14], scale from column lengths,
			// rotation from normalized basis. Handles non-skewed TRS matrices.
			const float* m = n.matrix;
			trs.t[0] = m[12]; trs.t[1] = m[13]; trs.t[2] = m[14];
			float sx = std::sqrt(m[0]*m[0] + m[1]*m[1] + m[2]*m[2]);
			float sy = std::sqrt(m[4]*m[4] + m[5]*m[5] + m[6]*m[6]);
			float sz = std::sqrt(m[8]*m[8] + m[9]*m[9] + m[10]*m[10]);
			trs.s[0] = sx; trs.s[1] = sy; trs.s[2] = sz;
			float rm[9] = {
				sx>1e-6f ? m[0]/sx : 1.0f,
				sx>1e-6f ? m[1]/sx : 0.0f,
				sx>1e-6f ? m[2]/sx : 0.0f,
				sy>1e-6f ? m[4]/sy : 0.0f,
				sy>1e-6f ? m[5]/sy : 1.0f,
				sy>1e-6f ? m[6]/sy : 0.0f,
				sz>1e-6f ? m[8]/sz : 0.0f,
				sz>1e-6f ? m[9]/sz : 0.0f,
				sz>1e-6f ? m[10]/sz : 1.0f,
			};
			// Convert 3x3 rotation to quaternion.
			float trace = rm[0] + rm[4] + rm[8];
			if (trace > 0.0f) {
				float s = std::sqrt(trace + 1.0f) * 2.0f;
				trs.r[3] = 0.25f * s;
				trs.r[0] = (rm[5] - rm[7]) / s;
				trs.r[1] = (rm[6] - rm[2]) / s;
				trs.r[2] = (rm[1] - rm[3]) / s;
			} else if (rm[0] > rm[4] && rm[0] > rm[8]) {
				float s = std::sqrt(1.0f + rm[0] - rm[4] - rm[8]) * 2.0f;
				trs.r[3] = (rm[5] - rm[7]) / s;
				trs.r[0] = 0.25f * s;
				trs.r[1] = (rm[3] + rm[1]) / s;
				trs.r[2] = (rm[6] + rm[2]) / s;
			} else if (rm[4] > rm[8]) {
				float s = std::sqrt(1.0f + rm[4] - rm[0] - rm[8]) * 2.0f;
				trs.r[3] = (rm[6] - rm[2]) / s;
				trs.r[0] = (rm[3] + rm[1]) / s;
				trs.r[1] = 0.25f * s;
				trs.r[2] = (rm[7] + rm[5]) / s;
			} else {
				float s = std::sqrt(1.0f + rm[8] - rm[0] - rm[4]) * 2.0f;
				trs.r[3] = (rm[1] - rm[3]) / s;
				trs.r[0] = (rm[6] + rm[2]) / s;
				trs.r[1] = (rm[7] + rm[5]) / s;
				trs.r[2] = 0.25f * s;
			}
			quat_normalize(trs.r);
		} else {
			if (n.has_translation) {
				trs.t[0] = n.translation[0];
				trs.t[1] = n.translation[1];
				trs.t[2] = n.translation[2];
			}
			if (n.has_rotation) {
				trs.r[0] = n.rotation[0];
				trs.r[1] = n.rotation[1];
				trs.r[2] = n.rotation[2];
				trs.r[3] = n.rotation[3];
			}
			if (n.has_scale) {
				trs.s[0] = n.scale[0];
				trs.s[1] = n.scale[1];
				trs.s[2] = n.scale[2];
			}
		}
		nd.rest = trs;
		nd.current = trs;
		bx::mtxIdentity(nd.world);
	}

	// Topo order: BFS from each root in node-array order.
	s_->traversal.reserve(node_count);
	std::vector<uint8_t> visited(node_count, 0);
	std::vector<std::vector<int>> children(node_count);
	for (size_t i = 0; i < node_count; ++i) {
		int p = s_->nodes[i].parent;
		if (p >= 0) children[p].push_back(static_cast<int>(i));
	}
	for (size_t i = 0; i < node_count; ++i) {
		if (s_->nodes[i].parent < 0) {
			std::vector<int> stack;
			stack.push_back(static_cast<int>(i));
			while (!stack.empty()) {
				int n = stack.back(); stack.pop_back();
				if (visited[n]) continue;
				visited[n] = 1;
				s_->traversal.push_back(n);
				for (int c : children[n]) stack.push_back(c);
			}
		}
	}

	// Skin (assume one skin used by all skinned primitives).
	if (data->skins_count == 0) {
		cgltf_free(data);
		err = "glb has no skins";
		return false;
	}
	const cgltf_skin& skin = data->skins[0];
	const size_t joint_count = skin.joints_count;
	if (joint_count > k_max_bones) {
		std::fprintf(stderr, "viewmodel: skin has %zu joints, exceeds shader limit %d\n",
			joint_count, k_max_bones);
	}
	const size_t use_joints = std::min<size_t>(joint_count, k_max_bones);
	s_->joint_nodes.resize(use_joints);
	for (size_t i = 0; i < use_joints; ++i) {
		s_->joint_nodes[i] = static_cast<int>(skin.joints[i] - data->nodes);
	}
	s_->inv_bind.assign(use_joints * 16, 0.0f);
	if (skin.inverse_bind_matrices) {
		for (size_t i = 0; i < use_joints; ++i) {
			cgltf_accessor_read_float(skin.inverse_bind_matrices, i, &s_->inv_bind[i * 16], 16);
		}
	} else {
		for (size_t i = 0; i < use_joints; ++i) {
			bx::mtxIdentity(&s_->inv_bind[i * 16]);
		}
	}

	// Cache decoded textures by image index. The State owns each handle exactly once.
	std::unordered_map<size_t, bgfx::TextureHandle> image_cache;
	auto get_image_texture = [&](const cgltf_image* img) -> bgfx::TextureHandle {
		if (!img) return BGFX_INVALID_HANDLE;
		size_t idx = static_cast<size_t>(img - data->images);
		auto it = image_cache.find(idx);
		if (it != image_cache.end()) return it->second;
		std::fprintf(stderr, "  img[%zu]: mime=%s bv=%s uri=%s\n",
			idx,
			img->mime_type ? img->mime_type : "(none)",
			img->buffer_view ? "yes" : "null",
			img->uri ? img->uri : "(none)");
		if (!img->buffer_view) {
			image_cache[idx] = BGFX_INVALID_HANDLE;
			return BGFX_INVALID_HANDLE;
		}
		const cgltf_buffer_view* bv = img->buffer_view;
		const uint8_t* base = static_cast<const uint8_t*>(bv->buffer->data) + bv->offset;
		bgfx::TextureHandle h = decode_texture_from_memory(base, bv->size, 0);
		image_cache[idx] = h;
		if (bgfx::isValid(h)) s_->owned_textures.push_back(h);
		return h;
	};

	// Primitives — only skinned ones (have JOINTS_0 + WEIGHTS_0).
	for (size_t mi = 0; mi < data->meshes_count; ++mi) {
		const cgltf_mesh& mesh = data->meshes[mi];
		for (size_t pi = 0; pi < mesh.primitives_count; ++pi) {
			const cgltf_primitive& prim = mesh.primitives[pi];

			const cgltf_accessor* accPos = nullptr;
			const cgltf_accessor* accNrm = nullptr;
			const cgltf_accessor* accUv = nullptr;
			const cgltf_accessor* accJoints = nullptr;
			const cgltf_accessor* accWeights = nullptr;

			for (size_t ai = 0; ai < prim.attributes_count; ++ai) {
				const cgltf_attribute& a = prim.attributes[ai];
				switch (a.type) {
					case cgltf_attribute_type_position:  accPos = a.data; break;
					case cgltf_attribute_type_normal:    accNrm = a.data; break;
					case cgltf_attribute_type_texcoord:
						if (a.index == 0) accUv = a.data;
						break;
					case cgltf_attribute_type_joints:
						if (a.index == 0) accJoints = a.data;
						break;
					case cgltf_attribute_type_weights:
						if (a.index == 0) accWeights = a.data;
						break;
					default: break;
				}
			}

			if (!accPos || !accJoints || !accWeights) {
				continue; // skip non-skinned primitives for now
			}

			// Diagnostics: position accessor metadata (especially for the gun primitive).
			std::printf("    accPos: count=%zu type=%d component_type=%d normalized=%d "
				"min=(%.2f,%.2f,%.2f) max=(%.2f,%.2f,%.2f)\n",
				accPos->count,
				static_cast<int>(accPos->type),
				static_cast<int>(accPos->component_type),
				accPos->normalized ? 1 : 0,
				accPos->has_min ? accPos->min[0] : 0.0f,
				accPos->has_min ? accPos->min[1] : 0.0f,
				accPos->has_min ? accPos->min[2] : 0.0f,
				accPos->has_max ? accPos->max[0] : 0.0f,
				accPos->has_max ? accPos->max[1] : 0.0f,
				accPos->has_max ? accPos->max[2] : 0.0f);

			const size_t vcount = accPos->count;
			std::vector<SkinnedVertex> verts(vcount);
			for (size_t i = 0; i < vcount; ++i) {
				SkinnedVertex& v = verts[i];
				float p[3] = {0, 0, 0};
				cgltf_accessor_read_float(accPos, i, p, 3);
				v.x = p[0]; v.y = p[1]; v.z = p[2];
				if (accNrm) {
					float n[3] = {0, 1, 0};
					cgltf_accessor_read_float(accNrm, i, n, 3);
					v.nx = n[0]; v.ny = n[1]; v.nz = n[2];
				}
				if (accUv) {
					float uv[2] = {0, 0};
					cgltf_accessor_read_float(accUv, i, uv, 2);
					v.u = uv[0]; v.v = uv[1];
				}
				cgltf_uint j[4] = {0, 0, 0, 0};
				cgltf_accessor_read_uint(accJoints, i, j, 4);
				for (int k = 0; k < 4; ++k) {
					uint32_t ji = j[k];
					if (ji >= use_joints) ji = 0;
					v.joints[k] = static_cast<uint8_t>(ji);
				}
				float w[4] = {1, 0, 0, 0};
				cgltf_accessor_read_float(accWeights, i, w, 4);
				float ws = w[0] + w[1] + w[2] + w[3];
				if (ws > 1e-6f) {
					float inv = 1.0f / ws;
					w[0] *= inv; w[1] *= inv; w[2] *= inv; w[3] *= inv;
				} else {
					w[0] = 1.0f; w[1] = 0; w[2] = 0; w[3] = 0;
				}
				v.weights[0] = w[0];
				v.weights[1] = w[1];
				v.weights[2] = w[2];
				v.weights[3] = w[3];
			}

			Primitive p;
			const bgfx::Memory* vmem = bgfx::copy(
				verts.data(),
				static_cast<uint32_t>(verts.size() * sizeof(SkinnedVertex))
			);
			p.vb = bgfx::createVertexBuffer(vmem, s_->layout);

			if (prim.indices) {
				const cgltf_accessor* accIdx = prim.indices;
				const size_t icount = accIdx->count;
				if (icount > 0xFFFF || accIdx->component_type == cgltf_component_type_r_32u) {
					std::vector<uint32_t> idx(icount);
					for (size_t i = 0; i < icount; ++i) {
						cgltf_uint v = 0;
						cgltf_accessor_read_uint(accIdx, i, &v, 1);
						idx[i] = static_cast<uint32_t>(v);
					}
					const bgfx::Memory* imem = bgfx::copy(
						idx.data(), static_cast<uint32_t>(idx.size() * sizeof(uint32_t))
					);
					p.ib = bgfx::createIndexBuffer(imem, BGFX_BUFFER_INDEX32);
				} else {
					std::vector<uint16_t> idx(icount);
					for (size_t i = 0; i < icount; ++i) {
						cgltf_uint v = 0;
						cgltf_accessor_read_uint(accIdx, i, &v, 1);
						idx[i] = static_cast<uint16_t>(v);
					}
					const bgfx::Memory* imem = bgfx::copy(
						idx.data(), static_cast<uint32_t>(idx.size() * sizeof(uint16_t))
					);
					p.ib = bgfx::createIndexBuffer(imem);
				}
			}

			if (prim.material) {
				const cgltf_pbr_metallic_roughness& pbr = prim.material->pbr_metallic_roughness;
				if (prim.material->has_pbr_metallic_roughness) {
					p.base_color[0] = pbr.base_color_factor[0];
					p.base_color[1] = pbr.base_color_factor[1];
					p.base_color[2] = pbr.base_color_factor[2];
					p.base_color[3] = pbr.base_color_factor[3];
					if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image) {
						bgfx::TextureHandle h = get_image_texture(pbr.base_color_texture.texture->image);
						if (bgfx::isValid(h)) {
							p.texture = h;
						}
					}
				}
			}

			s_->primitives.push_back(p);
		}
	}

	// Animations.
	s_->animations.resize(data->animations_count);
	for (size_t ai = 0; ai < data->animations_count; ++ai) {
		const cgltf_animation& src = data->animations[ai];
		Animation& a = s_->animations[ai];
		a.name = src.name ? src.name : "";

		a.samplers.resize(src.samplers_count);
		for (size_t si = 0; si < src.samplers_count; ++si) {
			const cgltf_animation_sampler& ss = src.samplers[si];
			AnimSampler& d = a.samplers[si];
			const size_t in_count = ss.input->count;
			d.input.resize(in_count);
			for (size_t k = 0; k < in_count; ++k) {
				cgltf_accessor_read_float(ss.input, k, &d.input[k], 1);
			}
			int comps = static_cast<int>(cgltf_num_components(ss.output->type));
			d.components = comps;
			const size_t out_count = ss.output->count;
			d.output.resize(out_count * static_cast<size_t>(comps));
			for (size_t k = 0; k < out_count; ++k) {
				cgltf_accessor_read_float(
					ss.output, k, &d.output[k * static_cast<size_t>(comps)], comps
				);
			}
			switch (ss.interpolation) {
				case cgltf_interpolation_type_linear:      d.interp = 0; break;
				case cgltf_interpolation_type_step:        d.interp = 1; break;
				case cgltf_interpolation_type_cubic_spline:d.interp = 2; break;
				default: d.interp = 0; break;
			}
			if (!d.input.empty()) {
				a.duration = std::max(a.duration, d.input.back());
			}
		}

		a.channels.reserve(src.channels_count);
		for (size_t ci = 0; ci < src.channels_count; ++ci) {
			const cgltf_animation_channel& cc = src.channels[ci];
			AnimChannel d;
			if (!cc.target_node) continue;
			d.node = static_cast<int>(cc.target_node - data->nodes);
			d.sampler = static_cast<int>(cc.sampler - src.samplers);
			switch (cc.target_path) {
				case cgltf_animation_path_type_translation: d.path = AnimPath::Translation; break;
				case cgltf_animation_path_type_rotation:    d.path = AnimPath::Rotation; break;
				case cgltf_animation_path_type_scale:       d.path = AnimPath::Scale; break;
				default: continue;
			}
			a.channels.push_back(d);
		}

		ViewmodelAnim slot = classify_anim_name(a.name.c_str());
		if (slot != ViewmodelAnim::Count) {
			s_->anim_lookup[static_cast<int>(slot)] = static_cast<int>(ai);
		}
		ZombieAnim zslot = classify_zombie_anim_name(a.name.c_str());
		if (zslot != ZombieAnim::Count) {
			int& entry = s_->zombie_anim_lookup[static_cast<int>(zslot)];
			if (entry < 0) entry = static_cast<int>(ai); // first match wins
		}
	}

	cgltf_free(data);

	// Initial pose: rest TRS, no animation, world matrices computed once.
	for (auto& n : s_->nodes) n.current = n.rest;

	std::printf("viewmodel loaded: nodes=%zu joints=%zu primitives=%zu anims=%zu (idle=%d shoot=%d reload=%d take=%d) (z_idle=%d z_walk=%d z_run=%d z_attack=%d z_death=%d z_hit=%d)\n",
		s_->nodes.size(), s_->joint_nodes.size(), s_->primitives.size(), s_->animations.size(),
		s_->anim_lookup[0], s_->anim_lookup[1], s_->anim_lookup[2], s_->anim_lookup[3],
		s_->zombie_anim_lookup[0], s_->zombie_anim_lookup[1], s_->zombie_anim_lookup[2],
		s_->zombie_anim_lookup[3], s_->zombie_anim_lookup[4], s_->zombie_anim_lookup[5]);

	s_->loaded = true;
	return true;
}

void Viewmodel::update(float dt)
{
	if (!s_->loaded) return;

	// Reset to rest pose, then apply current animation.
	for (auto& n : s_->nodes) n.current = n.rest;

	if (s_->current >= 0 && s_->current < static_cast<int>(s_->animations.size())) {
		{
			const Animation& a = s_->animations[s_->current];
			s_->time += dt;
			if (a.duration > 0.0f) {
				if (s_->looping) {
					s_->time = std::fmod(s_->time, a.duration);
					if (s_->time < 0.0f) s_->time += a.duration;
				} else if (s_->time >= a.duration) {
					s_->time = a.duration;
					s_->finished = true;
				}
			}

			for (const AnimChannel& ch : a.channels) {
				if (ch.node < 0 || ch.node >= static_cast<int>(s_->nodes.size())) continue;
				const AnimSampler& smp = a.samplers[ch.sampler];
				if (smp.input.empty() || smp.components <= 0) continue;
				float alpha = 0.0f;
				int k0 = find_keyframe(smp.input, s_->time, alpha);
				int k1 = std::min(k0 + 1, static_cast<int>(smp.input.size()) - 1);

				int comps = smp.components;
				int stride = comps;
				int kf_stride = (smp.interp == 2) ? comps * 3 : comps; // CUBICSPLINE has in,v,out
				const float* v0 = nullptr;
				const float* v1 = nullptr;
				if (smp.interp == 2) {
					// Use middle component (the value) for cubicspline; ignore tangents.
					v0 = &smp.output[(static_cast<size_t>(k0) * 3 + 1) * static_cast<size_t>(comps)];
					v1 = &smp.output[(static_cast<size_t>(k1) * 3 + 1) * static_cast<size_t>(comps)];
				} else {
					v0 = &smp.output[static_cast<size_t>(k0) * static_cast<size_t>(stride)];
					v1 = &smp.output[static_cast<size_t>(k1) * static_cast<size_t>(stride)];
				}
				(void)kf_stride;

				NodeData& nd = s_->nodes[ch.node];
				if (smp.interp == 1) alpha = 0.0f; // STEP
				switch (ch.path) {
					case AnimPath::Translation:
						nd.current.t[0] = v0[0] + alpha * (v1[0] - v0[0]);
						nd.current.t[1] = v0[1] + alpha * (v1[1] - v0[1]);
						nd.current.t[2] = v0[2] + alpha * (v1[2] - v0[2]);
						break;
					case AnimPath::Scale:
						nd.current.s[0] = v0[0] + alpha * (v1[0] - v0[0]);
						nd.current.s[1] = v0[1] + alpha * (v1[1] - v0[1]);
						nd.current.s[2] = v0[2] + alpha * (v1[2] - v0[2]);
						break;
					case AnimPath::Rotation: {
						float q0[4] = {v0[0], v0[1], v0[2], v0[3]};
						float q1[4] = {v1[0], v1[1], v1[2], v1[3]};
						quat_slerp(q0, q1, alpha, nd.current.r);
						quat_normalize(nd.current.r);
						break;
					}
				}
			}
		}
	}

	// Build world matrices in topological order.
	for (int n : s_->traversal) {
		NodeData& nd = s_->nodes[n];
		float local[16];
		compute_local_matrix(nd.current, local);
		if (nd.parent < 0) {
			std::memcpy(nd.world, local, sizeof(local));
		} else {
			// world = parent_world * local
			mat_mul(nd.world, s_->nodes[nd.parent].world, local);
		}
	}

	// Build bone matrices: bone[i] = world[joint_node[i]] * inv_bind[i]
	const size_t njoints = s_->joint_nodes.size();
	for (size_t i = 0; i < njoints; ++i) {
		const float* w = s_->nodes[s_->joint_nodes[i]].world;
		const float* ib = &s_->inv_bind[i * 16];
		mat_mul(&s_->bone_matrices[i * 16], w, ib);
	}
	// Pad remaining bones with identity (already done at construction).
}

void Viewmodel::submit(
	bgfx::ViewId view_id,
	bgfx::ProgramHandle program,
	bgfx::UniformHandle u_bones,
	bgfx::UniformHandle s_albedo,
	bgfx::UniformHandle u_baseColor,
	bgfx::TextureHandle fallback_white,
	uint64_t state,
	const ViewmodelDrawParams& params
)
{
	if (!s_->loaded) return;

	// Build: model = T_eye * R_camera * T_offset * R_tweak * S_scale
	// Applied to vertex v: scale → tweak rotate → camera-space offset → camera rotate → eye.
	float scl[16], rx[16], ry[16], rz[16];
	bx::mtxScale(scl, params.scale, params.scale, params.scale);
	bx::mtxRotateX(rx, params.tweak_pitch);
	bx::mtxRotateY(ry, params.tweak_yaw);
	bx::mtxRotateZ(rz, params.tweak_roll);

	float rxz[16], rxyz[16], rotTweak[16];
	mat_mul(rxz, ry, rx);          // ry * rx
	mat_mul(rxyz, rxz, rz);        // ry * rx * rz
	mat_mul(rotTweak, rxyz, scl);  // R_tweak * S

	float trnOffset[16];
	bx::mtxIdentity(trnOffset);
	trnOffset[12] = params.offset[0];
	trnOffset[13] = params.offset[1];
	trnOffset[14] = params.offset[2];

	float local[16];
	mat_mul(local, trnOffset, rotTweak);  // T_offset * R_tweak * S

	float camPitch[16], camYaw[16], camRot[16];
	bx::mtxRotateX(camPitch, -params.pitch);
	bx::mtxRotateY(camYaw, params.yaw);
	mat_mul(camRot, camYaw, camPitch);    // R_y(yaw) * R_x(-pitch)

	float modelNoTrans[16];
	mat_mul(modelNoTrans, camRot, local); // R_camera * (T_offset * R_tweak * S)

	float model[16];
	std::memcpy(model, modelNoTrans, sizeof(model));
	// Adding to translation column = pre-multiply by T_eye → model = T_eye * existing.
	model[12] += params.eye[0];
	model[13] += params.eye[1];
	model[14] += params.eye[2];

	for (const Primitive& p : s_->primitives) {
		if (!bgfx::isValid(p.vb)) continue;
		bgfx::setState(state);
		bgfx::setTransform(model);
		bgfx::setUniform(u_bones, s_->bone_matrices.data(), k_max_bones);
		bgfx::setUniform(u_baseColor, p.base_color);
		bgfx::TextureHandle tex = bgfx::isValid(p.texture) ? p.texture : fallback_white;
		bgfx::setTexture(0, s_albedo, tex);
		bgfx::setVertexBuffer(0, p.vb);
		if (bgfx::isValid(p.ib)) bgfx::setIndexBuffer(p.ib);
		bgfx::submit(view_id, program);
	}
}

void Viewmodel::submit_world(
	bgfx::ViewId view_id,
	bgfx::ProgramHandle program,
	bgfx::UniformHandle u_bones,
	bgfx::UniformHandle s_albedo,
	bgfx::UniformHandle u_baseColor,
	bgfx::TextureHandle fallback_white,
	uint64_t state,
	const CharacterDrawParams& params
)
{
	if (!s_->loaded) return;

	// model = T_world * R_y(yaw) * S_scale
	float scl[16], rot[16], model[16];
	bx::mtxScale(scl, params.scale, params.scale, params.scale);
	bx::mtxRotateY(rot, params.yaw);
	float rs[16];
	mat_mul(rs, rot, scl);           // R * S
	bx::mtxTranslate(model, params.pos[0], params.pos[1], params.pos[2]);
	float tmp[16];
	std::memcpy(tmp, model, sizeof(tmp));
	mat_mul(model, tmp, rs);         // T * (R * S)

	for (const Primitive& p : s_->primitives) {
		if (!bgfx::isValid(p.vb)) continue;
		bgfx::setState(state);
		bgfx::setTransform(model);
		bgfx::setUniform(u_bones, s_->bone_matrices.data(), k_max_bones);
		bgfx::setUniform(u_baseColor, p.base_color);
		bgfx::TextureHandle tex = bgfx::isValid(p.texture) ? p.texture : fallback_white;
		bgfx::setTexture(0, s_albedo, tex);
		bgfx::setVertexBuffer(0, p.vb);
		if (bgfx::isValid(p.ib)) bgfx::setIndexBuffer(p.ib);
		bgfx::submit(view_id, program);
	}
}

void Viewmodel::submit_axes_gizmo(
	bgfx::ViewId view_id,
	bgfx::ProgramHandle debug_program,
	const ViewmodelDrawParams& params,
	float length
)
{
	if (!bgfx::isValid(debug_program)) return;

	// Same transform as submit(), but without scale so the line lengths stay in world meters
	// regardless of model scale.
	float rx[16], ry[16], rz[16], rxz[16], rxyz[16];
	bx::mtxRotateX(rx, params.tweak_pitch);
	bx::mtxRotateY(ry, params.tweak_yaw);
	bx::mtxRotateZ(rz, params.tweak_roll);
	mat_mul(rxz, ry, rx);
	mat_mul(rxyz, rxz, rz);

	float trnOffset[16];
	bx::mtxIdentity(trnOffset);
	trnOffset[12] = params.offset[0];
	trnOffset[13] = params.offset[1];
	trnOffset[14] = params.offset[2];

	float local[16];
	mat_mul(local, trnOffset, rxyz);

	float camPitch[16], camYaw[16], camRot[16];
	bx::mtxRotateX(camPitch, -params.pitch);
	bx::mtxRotateY(camYaw, params.yaw);
	mat_mul(camRot, camYaw, camPitch);

	float modelNoEye[16];
	mat_mul(modelNoEye, camRot, local);

	float model[16];
	std::memcpy(model, modelNoEye, sizeof(model));
	model[12] += params.eye[0];
	model[13] += params.eye[1];
	model[14] += params.eye[2];

	bgfx::VertexLayout dbg_layout;
	dbg_layout.begin()
		.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
		.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
		.end();

	if (bgfx::getAvailTransientVertexBuffer(6, dbg_layout) < 6) return;

	bgfx::TransientVertexBuffer tvb;
	bgfx::allocTransientVertexBuffer(&tvb, 6, dbg_layout);

	struct V { float x, y, z; uint32_t abgr; };
	V* v = reinterpret_cast<V*>(tvb.data);

	const uint32_t kRed   = 0xff0000ffu; // ABGR: A=ff, B=00, G=00, R=ff
	const uint32_t kGreen = 0xff00ff00u;
	const uint32_t kBlue  = 0xffff0000u;

	v[0] = {0.0f,   0.0f, 0.0f, kRed};
	v[1] = {length, 0.0f, 0.0f, kRed};
	v[2] = {0.0f, 0.0f,   0.0f, kGreen};
	v[3] = {0.0f, length, 0.0f, kGreen};
	v[4] = {0.0f, 0.0f, 0.0f,   kBlue};
	v[5] = {0.0f, 0.0f, length, kBlue};

	bgfx::setState(
		BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_PT_LINES
		| BGFX_STATE_DEPTH_TEST_ALWAYS
	);
	bgfx::setTransform(model);
	bgfx::setVertexBuffer(0, &tvb);
	bgfx::submit(view_id, debug_program);
}

} // namespace engine
