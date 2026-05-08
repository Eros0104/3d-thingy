#include "engine/rigged_model.hpp"
#include "engine/skinned_vertex.hpp"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/math.h>

#include <algorithm>
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

// bx::mtxMul(out, a, b) computes out = b*a. We use math notation out = A*B.
inline void mat_mul(float out[16], const float a[16], const float b[16]) {
  bx::mtxMul(out, b, a);
}

void quat_to_mtx(float m[16], const float q[4]) {
  const float x = q[0], y = q[1], z = q[2], w = q[3];
  m[0] = 1.0f - 2.0f * y * y - 2.0f * z * z;
  m[1] = 2.0f * x * y + 2.0f * z * w;
  m[2] = 2.0f * x * z - 2.0f * y * w;
  m[3] = 0.0f;
  m[4] = 2.0f * x * y - 2.0f * z * w;
  m[5] = 1.0f - 2.0f * x * x - 2.0f * z * z;
  m[6] = 2.0f * y * z + 2.0f * x * w;
  m[7] = 0.0f;
  m[8] = 2.0f * x * z + 2.0f * y * w;
  m[9] = 2.0f * y * z - 2.0f * x * w;
  m[10] = 1.0f - 2.0f * x * x - 2.0f * y * y;
  m[11] = 0.0f;
  m[12] = 0.0f;
  m[13] = 0.0f;
  m[14] = 0.0f;
  m[15] = 1.0f;
}

void compute_local_matrix(const LocalTRS &trs, float out[16]) {
  float r[16];
  quat_to_mtx(r, trs.r);
  out[0] = r[0] * trs.s[0];
  out[1] = r[1] * trs.s[0];
  out[2] = r[2] * trs.s[0];
  out[3] = 0.0f;
  out[4] = r[4] * trs.s[1];
  out[5] = r[5] * trs.s[1];
  out[6] = r[6] * trs.s[1];
  out[7] = 0.0f;
  out[8] = r[8] * trs.s[2];
  out[9] = r[9] * trs.s[2];
  out[10] = r[10] * trs.s[2];
  out[11] = 0.0f;
  out[12] = trs.t[0];
  out[13] = trs.t[1];
  out[14] = trs.t[2];
  out[15] = 1.0f;
}

void quat_normalize(float q[4]) {
  float n2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
  if (n2 > 1e-12f) {
    float inv = 1.0f / std::sqrt(n2);
    q[0] *= inv;
    q[1] *= inv;
    q[2] *= inv;
    q[3] *= inv;
  } else {
    q[0] = 0.0f;
    q[1] = 0.0f;
    q[2] = 0.0f;
    q[3] = 1.0f;
  }
}

void quat_slerp(const float a[4], const float b[4], float t, float out[4]) {
  float bn[4] = {b[0], b[1], b[2], b[3]};
  float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
  if (dot < 0.0f) {
    bn[0] = -b[0];
    bn[1] = -b[1];
    bn[2] = -b[2];
    bn[3] = -b[3];
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

int find_keyframe(const std::vector<float> &input, float t, float &alpha) {
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
  size_t lo = 0, hi = input.size() - 1;
  while (lo + 1 < hi) {
    size_t mid = (lo + hi) / 2;
    if (input[mid] <= t)
      lo = mid;
    else
      hi = mid;
  }
  float dt = input[lo + 1] - input[lo];
  alpha = dt > 1e-6f ? (t - input[lo]) / dt : 0.0f;
  return static_cast<int>(lo);
}

bgfx::TextureHandle decode_texture_from_memory(const uint8_t *data, size_t size,
                                               uint64_t flags) {
  if (!data || size == 0)
    return BGFX_INVALID_HANDLE;
  bx::DefaultAllocator alloc;
  bimg::ImageContainer *img = bimg::imageParse(
      &alloc, data, static_cast<uint32_t>(size), bimg::TextureFormat::RGBA8);
  if (!img)
    return BGFX_INVALID_HANDLE;
  if (img->m_cubeMap || img->m_depth > 1 || !img->m_data) {
    bimg::imageFree(img);
    return BGFX_INVALID_HANDLE;
  }
  const bgfx::Memory *mem = bgfx::copy(img->m_data, img->m_size);
  bgfx::TextureHandle h = bgfx::createTexture2D(
      static_cast<uint16_t>(img->m_width), static_cast<uint16_t>(img->m_height),
      img->m_numMips > 1, static_cast<uint16_t>(img->m_numLayers),
      static_cast<bgfx::TextureFormat::Enum>(img->m_format), flags, mem);
  bimg::imageFree(img);
  return h;
}

} // namespace

// ============================================================
// State
// ============================================================

struct RiggedModel::State {
  bgfx::VertexLayout layout;
  std::vector<NodeData> nodes;
  std::vector<int> joint_nodes;
  std::vector<float> inv_bind;
  std::vector<int> traversal;
  std::vector<Primitive> primitives;
  std::vector<bgfx::TextureHandle> owned_textures;
  std::vector<Animation> animations;

  int current = -1;
  float time = 0.0f;
  bool looping = true;
  bool finished = false;

  std::vector<LocalTRS> prev_pose;
  float crossfade_dur = 0.15f;
  float crossfade_remaining = 0.0f;
  bool crossfading = false;

  std::vector<float> bone_matrices;
  bool loaded = false;

  void start_crossfade(int new_raw) {
    if (new_raw == current || current < 0 || prev_pose.empty())
      return;
    const size_t n = std::min(nodes.size(), prev_pose.size());
    for (size_t i = 0; i < n; ++i)
      prev_pose[i] = nodes[i].current;
    crossfade_remaining = crossfade_dur;
    crossfading = true;
  }
};

// ============================================================
// Construction
// ============================================================

RiggedModel::RiggedModel() : s_(new State()) {
  s_->bone_matrices.assign(static_cast<size_t>(k_max_bones) * 16, 0.0f);
  for (int i = 0; i < k_max_bones; ++i)
    bx::mtxIdentity(&s_->bone_matrices[static_cast<size_t>(i) * 16]);

  s_->layout.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      // Metal rejects int4 as MTLAttributeFormatUChar4; normalized=true gives
      // 0..1 float, rescaled to 0..255 in the shader.
      .add(bgfx::Attrib::Indices, 4, bgfx::AttribType::Uint8, true, false)
      .add(bgfx::Attrib::Weight, 4, bgfx::AttribType::Float)
      .end();
}

RiggedModel::~RiggedModel() {
  unload();
  delete s_;
  s_ = nullptr;
}

// ============================================================
// Load / unload
// ============================================================

void RiggedModel::unload() {
  for (auto &p : s_->primitives) {
    if (bgfx::isValid(p.vb))
      bgfx::destroy(p.vb);
    if (bgfx::isValid(p.ib))
      bgfx::destroy(p.ib);
  }
  for (auto &t : s_->owned_textures)
    if (bgfx::isValid(t))
      bgfx::destroy(t);

  s_->primitives.clear();
  s_->owned_textures.clear();
  s_->nodes.clear();
  s_->joint_nodes.clear();
  s_->inv_bind.clear();
  s_->traversal.clear();
  s_->animations.clear();
  s_->prev_pose.clear();
  s_->crossfading = false;
  s_->current = -1;
  s_->loaded = false;
}

bool RiggedModel::valid() const { return s_->loaded; }

bool RiggedModel::load(const char *glb_path, std::string &err) {
  cgltf_options opts{};
  cgltf_data *data = nullptr;
  if (cgltf_parse_file(&opts, glb_path, &data) != cgltf_result_success) {
    err = std::string("cgltf_parse_file failed: ") + glb_path;
    return false;
  }
  if (cgltf_load_buffers(&opts, data, glb_path) != cgltf_result_success) {
    cgltf_free(data);
    err = "cgltf_load_buffers failed";
    return false;
  }

  // --- Nodes ---
  const size_t node_count = data->nodes_count;
  s_->nodes.resize(node_count);
  for (size_t i = 0; i < node_count; ++i) {
    const cgltf_node &n = data->nodes[i];
    NodeData &nd = s_->nodes[i];
    nd.parent = n.parent ? static_cast<int>(n.parent - data->nodes) : -1;

    LocalTRS trs;
    if (n.has_matrix) {
      const float *m = n.matrix;
      trs.t[0] = m[12];
      trs.t[1] = m[13];
      trs.t[2] = m[14];
      float sx = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
      float sy = std::sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
      float sz = std::sqrt(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]);
      trs.s[0] = sx;
      trs.s[1] = sy;
      trs.s[2] = sz;
      float rm[9] = {
          sx > 1e-6f ? m[0] / sx : 1.f,  sx > 1e-6f ? m[1] / sx : 0.f,
          sx > 1e-6f ? m[2] / sx : 0.f,  sy > 1e-6f ? m[4] / sy : 0.f,
          sy > 1e-6f ? m[5] / sy : 1.f,  sy > 1e-6f ? m[6] / sy : 0.f,
          sz > 1e-6f ? m[8] / sz : 0.f,  sz > 1e-6f ? m[9] / sz : 0.f,
          sz > 1e-6f ? m[10] / sz : 1.f,
      };
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

  // --- Topological traversal order (BFS from roots) ---
  s_->traversal.reserve(node_count);
  std::vector<uint8_t> visited(node_count, 0);
  std::vector<std::vector<int>> children(node_count);
  for (size_t i = 0; i < node_count; ++i) {
    int p = s_->nodes[i].parent;
    if (p >= 0)
      children[p].push_back(static_cast<int>(i));
  }
  for (size_t i = 0; i < node_count; ++i) {
    if (s_->nodes[i].parent >= 0)
      continue;
    std::vector<int> stack = {static_cast<int>(i)};
    while (!stack.empty()) {
      int n = stack.back();
      stack.pop_back();
      if (visited[n])
        continue;
      visited[n] = 1;
      s_->traversal.push_back(n);
      for (int c : children[n])
        stack.push_back(c);
    }
  }

  // --- Skin ---
  if (data->skins_count == 0) {
    cgltf_free(data);
    err = "glb has no skins";
    return false;
  }
  const cgltf_skin &skin = data->skins[0];
  const size_t joint_count = skin.joints_count;
  const size_t use_joints = std::min<size_t>(joint_count, k_max_bones);
  s_->joint_nodes.resize(use_joints);
  for (size_t i = 0; i < use_joints; ++i)
    s_->joint_nodes[i] = static_cast<int>(skin.joints[i] - data->nodes);
  s_->inv_bind.assign(use_joints * 16, 0.0f);
  if (skin.inverse_bind_matrices) {
    for (size_t i = 0; i < use_joints; ++i)
      cgltf_accessor_read_float(skin.inverse_bind_matrices, i,
                                &s_->inv_bind[i * 16], 16);
  } else {
    for (size_t i = 0; i < use_joints; ++i)
      bx::mtxIdentity(&s_->inv_bind[i * 16]);
  }

  // --- Textures (cached by image index) ---
  std::unordered_map<size_t, bgfx::TextureHandle> image_cache;
  auto get_texture = [&](const cgltf_image *img) -> bgfx::TextureHandle {
    if (!img)
      return BGFX_INVALID_HANDLE;
    size_t idx = static_cast<size_t>(img - data->images);
    auto it = image_cache.find(idx);
    if (it != image_cache.end())
      return it->second;
    if (!img->buffer_view || !img->buffer_view->buffer ||
        !img->buffer_view->buffer->data) {
      image_cache[idx] = BGFX_INVALID_HANDLE;
      return BGFX_INVALID_HANDLE;
    }
    const uint8_t *base =
        static_cast<const uint8_t *>(img->buffer_view->buffer->data) +
        img->buffer_view->offset;
    bgfx::TextureHandle h =
        decode_texture_from_memory(base, img->buffer_view->size, 0);
    image_cache[idx] = h;
    if (bgfx::isValid(h))
      s_->owned_textures.push_back(h);
    return h;
  };

  // --- Primitives (skinned only) ---
  for (size_t mi = 0; mi < data->meshes_count; ++mi) {
    const cgltf_mesh &mesh = data->meshes[mi];
    for (size_t pi = 0; pi < mesh.primitives_count; ++pi) {
      const cgltf_primitive &prim = mesh.primitives[pi];
      const cgltf_accessor *accPos = nullptr, *accNrm = nullptr,
                           *accUv = nullptr, *accJoints = nullptr,
                           *accWeights = nullptr;
      for (size_t ai = 0; ai < prim.attributes_count; ++ai) {
        const cgltf_attribute &a = prim.attributes[ai];
        switch (a.type) {
        case cgltf_attribute_type_position:
          accPos = a.data;
          break;
        case cgltf_attribute_type_normal:
          accNrm = a.data;
          break;
        case cgltf_attribute_type_texcoord:
          if (a.index == 0)
            accUv = a.data;
          break;
        case cgltf_attribute_type_joints:
          if (a.index == 0)
            accJoints = a.data;
          break;
        case cgltf_attribute_type_weights:
          if (a.index == 0)
            accWeights = a.data;
          break;
        default:
          break;
        }
      }
      if (!accPos || !accJoints || !accWeights)
        continue;

      const size_t vcount = accPos->count;
      std::vector<SkinnedVertex> verts(vcount);
      for (size_t i = 0; i < vcount; ++i) {
        SkinnedVertex &v = verts[i];
        float p[3] = {0, 0, 0};
        cgltf_accessor_read_float(accPos, i, p, 3);
        v.x = p[0];
        v.y = p[1];
        v.z = p[2];
        if (accNrm) {
          float n[3] = {0, 1, 0};
          cgltf_accessor_read_float(accNrm, i, n, 3);
          v.nx = n[0];
          v.ny = n[1];
          v.nz = n[2];
        }
        if (accUv) {
          float uv[2] = {0, 0};
          cgltf_accessor_read_float(accUv, i, uv, 2);
          v.u = uv[0];
          v.v = uv[1];
        }
        cgltf_uint j[4] = {0, 0, 0, 0};
        cgltf_accessor_read_uint(accJoints, i, j, 4);
        for (int k = 0; k < 4; ++k) {
          uint32_t ji = j[k];
          if (ji >= use_joints)
            ji = 0;
          v.joints[k] = static_cast<uint8_t>(ji);
        }
        float w[4] = {1, 0, 0, 0};
        cgltf_accessor_read_float(accWeights, i, w, 4);
        float ws = w[0] + w[1] + w[2] + w[3];
        if (ws > 1e-6f) {
          float inv = 1.0f / ws;
          w[0] *= inv;
          w[1] *= inv;
          w[2] *= inv;
          w[3] *= inv;
        } else {
          w[0] = 1;
          w[1] = w[2] = w[3] = 0;
        }
        v.weights[0] = w[0];
        v.weights[1] = w[1];
        v.weights[2] = w[2];
        v.weights[3] = w[3];
      }

      Primitive p;
      p.vb = bgfx::createVertexBuffer(
          bgfx::copy(verts.data(), uint32_t(vcount * sizeof(SkinnedVertex))),
          s_->layout);

      if (prim.indices) {
        const cgltf_accessor *accIdx = prim.indices;
        const size_t icount = accIdx->count;
        if (icount > 0xFFFF ||
            accIdx->component_type == cgltf_component_type_r_32u) {
          std::vector<uint32_t> idx(icount);
          for (size_t i = 0; i < icount; ++i) {
            cgltf_uint v = 0;
            cgltf_accessor_read_uint(accIdx, i, &v, 1);
            idx[i] = v;
          }
          p.ib = bgfx::createIndexBuffer(
              bgfx::copy(idx.data(), uint32_t(icount * 4)),
              BGFX_BUFFER_INDEX32);
        } else {
          std::vector<uint16_t> idx(icount);
          for (size_t i = 0; i < icount; ++i) {
            cgltf_uint v = 0;
            cgltf_accessor_read_uint(accIdx, i, &v, 1);
            idx[i] = uint16_t(v);
          }
          p.ib = bgfx::createIndexBuffer(
              bgfx::copy(idx.data(), uint32_t(icount * 2)));
        }
      }

      if (prim.material && prim.material->has_pbr_metallic_roughness) {
        const cgltf_pbr_metallic_roughness &pbr =
            prim.material->pbr_metallic_roughness;
        for (int k = 0; k < 4; ++k)
          p.base_color[k] = pbr.base_color_factor[k];
        if (pbr.base_color_texture.texture &&
            pbr.base_color_texture.texture->image) {
          bgfx::TextureHandle h =
              get_texture(pbr.base_color_texture.texture->image);
          if (bgfx::isValid(h))
            p.texture = h;
        }
      }
      s_->primitives.push_back(p);
    }
  }

  // --- Animations ---
  s_->animations.resize(data->animations_count);
  for (size_t ai = 0; ai < data->animations_count; ++ai) {
    const cgltf_animation &src = data->animations[ai];
    Animation &a = s_->animations[ai];
    a.name = src.name ? src.name : "";

    a.samplers.resize(src.samplers_count);
    for (size_t si = 0; si < src.samplers_count; ++si) {
      const cgltf_animation_sampler &ss = src.samplers[si];
      AnimSampler &d = a.samplers[si];
      d.input.resize(ss.input->count);
      for (size_t k = 0; k < ss.input->count; ++k)
        cgltf_accessor_read_float(ss.input, k, &d.input[k], 1);
      d.components = static_cast<int>(cgltf_num_components(ss.output->type));
      d.output.resize(ss.output->count * static_cast<size_t>(d.components));
      for (size_t k = 0; k < ss.output->count; ++k)
        cgltf_accessor_read_float(
            ss.output, k, &d.output[k * static_cast<size_t>(d.components)],
            d.components);
      switch (ss.interpolation) {
      case cgltf_interpolation_type_step:
        d.interp = 1;
        break;
      case cgltf_interpolation_type_cubic_spline:
        d.interp = 2;
        break;
      default:
        d.interp = 0;
        break;
      }
      if (!d.input.empty())
        a.duration = std::max(a.duration, d.input.back());
    }

    a.channels.reserve(src.channels_count);
    for (size_t ci = 0; ci < src.channels_count; ++ci) {
      const cgltf_animation_channel &cc = src.channels[ci];
      if (!cc.target_node)
        continue;
      AnimChannel d;
      d.node = static_cast<int>(cc.target_node - data->nodes);
      d.sampler = static_cast<int>(cc.sampler - src.samplers);
      switch (cc.target_path) {
      case cgltf_animation_path_type_translation:
        d.path = AnimPath::Translation;
        break;
      case cgltf_animation_path_type_rotation:
        d.path = AnimPath::Rotation;
        break;
      case cgltf_animation_path_type_scale:
        d.path = AnimPath::Scale;
        break;
      default:
        continue;
      }
      a.channels.push_back(d);
    }
  }

  cgltf_free(data);

  for (auto &n : s_->nodes)
    n.current = n.rest;
  s_->prev_pose.resize(s_->nodes.size());
  for (size_t i = 0; i < s_->nodes.size(); ++i)
    s_->prev_pose[i] = s_->nodes[i].rest;

  s_->loaded = true;
  return true;
}

// ============================================================
// Playback
// ============================================================

void RiggedModel::play_raw(int idx, bool loop, bool restart_if_same,
                           bool crossfade) {
  if (!s_->loaded || idx < 0 || idx >= static_cast<int>(s_->animations.size()))
    return;
  if (idx == s_->current && !restart_if_same) {
    s_->looping = loop;
    return;
  }
  if (crossfade)
    s_->start_crossfade(idx);
  s_->current = idx;
  s_->time = 0.0f;
  s_->looping = loop;
  s_->finished = false;
}

int RiggedModel::current_raw() const { return s_->current; }
bool RiggedModel::current_finished() const { return s_->finished; }
int RiggedModel::anim_count() const {
  return static_cast<int>(s_->animations.size());
}
const char *RiggedModel::anim_name(int i) const {
  if (i < 0 || i >= anim_count())
    return "";
  return s_->animations[static_cast<size_t>(i)].name.c_str();
}

// ============================================================
// Update
// ============================================================

void RiggedModel::update(float dt) {
  if (!s_->loaded)
    return;

  for (auto &n : s_->nodes)
    n.current = n.rest;

  if (s_->current >= 0 &&
      s_->current < static_cast<int>(s_->animations.size())) {
    const Animation &a = s_->animations[s_->current];
    s_->time += dt;
    if (a.duration > 0.0f) {
      if (s_->looping) {
        s_->time = std::fmod(s_->time, a.duration);
        if (s_->time < 0.0f)
          s_->time += a.duration;
      } else if (s_->time >= a.duration) {
        s_->time = a.duration;
        s_->finished = true;
      }
    }

    for (const AnimChannel &ch : a.channels) {
      if (ch.node < 0 || ch.node >= static_cast<int>(s_->nodes.size()))
        continue;
      const AnimSampler &smp = a.samplers[ch.sampler];
      if (smp.input.empty() || smp.components <= 0)
        continue;

      float alpha = 0.0f;
      int k0 = find_keyframe(smp.input, s_->time, alpha);
      int k1 = std::min(k0 + 1, static_cast<int>(smp.input.size()) - 1);
      if (smp.interp == 1)
        alpha = 0.0f; // STEP

      const float *v0;
      const float *v1;
      const int comps = smp.components;
      if (smp.interp ==
          2) { // CUBICSPLINE: use middle (value) component, ignore tangents
        v0 = &smp.output[(static_cast<size_t>(k0) * 3 + 1) *
                         static_cast<size_t>(comps)];
        v1 = &smp.output[(static_cast<size_t>(k1) * 3 + 1) *
                         static_cast<size_t>(comps)];
      } else {
        v0 = &smp.output[static_cast<size_t>(k0) * static_cast<size_t>(comps)];
        v1 = &smp.output[static_cast<size_t>(k1) * static_cast<size_t>(comps)];
      }

      NodeData &nd = s_->nodes[ch.node];
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
        float q0[4] = {v0[0], v0[1], v0[2], v0[3]},
              q1[4] = {v1[0], v1[1], v1[2], v1[3]};
        quat_slerp(q0, q1, alpha, nd.current.r);
        quat_normalize(nd.current.r);
        break;
      }
      }
    }
  }

  // Crossfade: blend prev_pose → current
  if (s_->crossfading && !s_->prev_pose.empty()) {
    s_->crossfade_remaining -= dt;
    if (s_->crossfade_remaining <= 0.0f) {
      s_->crossfading = false;
    } else {
      const float alpha = 1.0f - s_->crossfade_remaining / s_->crossfade_dur;
      const size_t n = std::min(s_->nodes.size(), s_->prev_pose.size());
      for (size_t i = 0; i < n; ++i) {
        NodeData &nd = s_->nodes[i];
        const LocalTRS &prev = s_->prev_pose[i];
        nd.current.t[0] = prev.t[0] + alpha * (nd.current.t[0] - prev.t[0]);
        nd.current.t[1] = prev.t[1] + alpha * (nd.current.t[1] - prev.t[1]);
        nd.current.t[2] = prev.t[2] + alpha * (nd.current.t[2] - prev.t[2]);
        nd.current.s[0] = prev.s[0] + alpha * (nd.current.s[0] - prev.s[0]);
        nd.current.s[1] = prev.s[1] + alpha * (nd.current.s[1] - prev.s[1]);
        nd.current.s[2] = prev.s[2] + alpha * (nd.current.s[2] - prev.s[2]);
        float q0[4] = {prev.r[0], prev.r[1], prev.r[2], prev.r[3]};
        float q1[4] = {nd.current.r[0], nd.current.r[1], nd.current.r[2],
                       nd.current.r[3]};
        quat_slerp(q0, q1, alpha, nd.current.r);
        quat_normalize(nd.current.r);
      }
    }
  }

  // World matrices (topological order)
  for (int ni : s_->traversal) {
    NodeData &nd = s_->nodes[ni];
    float local[16];
    compute_local_matrix(nd.current, local);
    if (nd.parent < 0) {
      std::memcpy(nd.world, local, sizeof(local));
    } else {
      mat_mul(nd.world, s_->nodes[nd.parent].world, local);
    }
  }

  // Bone matrices: bone[i] = world[joint_node[i]] * inv_bind[i]
  const size_t nj = s_->joint_nodes.size();
  for (size_t i = 0; i < nj; ++i)
    mat_mul(&s_->bone_matrices[i * 16], s_->nodes[s_->joint_nodes[i]].world,
            &s_->inv_bind[i * 16]);
}

// ============================================================
// Render
// ============================================================

void RiggedModel::submit_viewmodel(
    bgfx::ViewId view_id, bgfx::ProgramHandle program,
    bgfx::UniformHandle u_bones, bgfx::UniformHandle s_albedo,
    bgfx::UniformHandle u_baseColor, bgfx::TextureHandle fallback_white,
    uint64_t state, const ViewmodelDrawParams &p) {
  if (!s_->loaded)
    return;

  // model = T_eye * R_camera * T_offset * R_tweak * S_scale
  float scl[16], rx[16], ry[16], rz[16];
  bx::mtxScale(scl, p.scale, p.scale, p.scale);
  bx::mtxRotateX(rx, p.tweak_pitch);
  bx::mtxRotateY(ry, p.tweak_yaw);
  bx::mtxRotateZ(rz, p.tweak_roll);
  float rxz[16], rxyz[16], rotTweak[16];
  mat_mul(rxz, ry, rx);
  mat_mul(rxyz, rxz, rz);
  mat_mul(rotTweak, rxyz, scl);

  float trnOffset[16];
  bx::mtxIdentity(trnOffset);
  trnOffset[12] = p.offset[0];
  trnOffset[13] = p.offset[1];
  trnOffset[14] = p.offset[2];
  float local[16];
  mat_mul(local, trnOffset, rotTweak);

  float camPitch[16], camYaw[16], camRot[16];
  bx::mtxRotateX(camPitch, -p.pitch);
  bx::mtxRotateY(camYaw, p.yaw);
  mat_mul(camRot, camYaw, camPitch);

  float model[16];
  mat_mul(model, camRot, local);
  model[12] += p.eye[0];
  model[13] += p.eye[1];
  model[14] += p.eye[2];

  for (const Primitive &prim : s_->primitives) {
    if (!bgfx::isValid(prim.vb))
      continue;
    bgfx::setState(state);
    bgfx::setTransform(model);
    bgfx::setUniform(u_bones, s_->bone_matrices.data(), k_max_bones);
    bgfx::setUniform(u_baseColor, prim.base_color);
    bgfx::setTexture(0, s_albedo,
                     bgfx::isValid(prim.texture) ? prim.texture
                                                 : fallback_white);
    bgfx::setVertexBuffer(0, prim.vb);
    if (bgfx::isValid(prim.ib))
      bgfx::setIndexBuffer(prim.ib);
    bgfx::submit(view_id, program);
  }
}

void RiggedModel::submit_world(bgfx::ViewId view_id,
                               bgfx::ProgramHandle program,
                               bgfx::UniformHandle u_bones,
                               bgfx::UniformHandle s_albedo,
                               bgfx::UniformHandle u_baseColor,
                               bgfx::TextureHandle fallback_white,
                               uint64_t state, const CharacterDrawParams &p) {
  if (!s_->loaded)
    return;

  // model = T_world * R_y(yaw) * S_scale
  float scl[16], rot[16], rs[16], t[16], model[16];
  bx::mtxScale(scl, p.scale, p.scale, p.scale);
  bx::mtxRotateY(rot, p.yaw);
  mat_mul(rs, rot, scl);
  bx::mtxTranslate(t, p.pos[0], p.pos[1], p.pos[2]);
  mat_mul(model, t, rs);

  for (const Primitive &prim : s_->primitives) {
    if (!bgfx::isValid(prim.vb))
      continue;
    bgfx::setState(state);
    bgfx::setTransform(model);
    bgfx::setUniform(u_bones, s_->bone_matrices.data(), k_max_bones);
    bgfx::setUniform(u_baseColor, prim.base_color);
    bgfx::setTexture(0, s_albedo,
                     bgfx::isValid(prim.texture) ? prim.texture
                                                 : fallback_white);
    bgfx::setVertexBuffer(0, prim.vb);
    if (bgfx::isValid(prim.ib))
      bgfx::setIndexBuffer(prim.ib);
    bgfx::submit(view_id, program);
  }
}

void RiggedModel::submit_axes_gizmo(bgfx::ViewId view_id,
                                    bgfx::ProgramHandle debug_program,
                                    const ViewmodelDrawParams &p,
                                    float length) {
  if (!bgfx::isValid(debug_program))
    return;

  float rx[16], ry[16], rz[16], rxz[16], rxyz[16];
  bx::mtxRotateX(rx, p.tweak_pitch);
  bx::mtxRotateY(ry, p.tweak_yaw);
  bx::mtxRotateZ(rz, p.tweak_roll);
  mat_mul(rxz, ry, rx);
  mat_mul(rxyz, rxz, rz);

  float trnOffset[16];
  bx::mtxIdentity(trnOffset);
  trnOffset[12] = p.offset[0];
  trnOffset[13] = p.offset[1];
  trnOffset[14] = p.offset[2];
  float local[16];
  mat_mul(local, trnOffset, rxyz);

  float camPitch[16], camYaw[16], camRot[16];
  bx::mtxRotateX(camPitch, -p.pitch);
  bx::mtxRotateY(camYaw, p.yaw);
  mat_mul(camRot, camYaw, camPitch);

  float model[16];
  mat_mul(model, camRot, local);
  model[12] += p.eye[0];
  model[13] += p.eye[1];
  model[14] += p.eye[2];

  bgfx::VertexLayout dbg_layout;
  dbg_layout.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .end();
  if (bgfx::getAvailTransientVertexBuffer(6, dbg_layout) < 6)
    return;

  bgfx::TransientVertexBuffer tvb;
  bgfx::allocTransientVertexBuffer(&tvb, 6, dbg_layout);
  struct V {
    float x, y, z;
    uint32_t abgr;
  };
  V *v = reinterpret_cast<V *>(tvb.data);
  v[0] = {0, 0, 0, 0xff0000ffu};
  v[1] = {length, 0, 0, 0xff0000ffu};
  v[2] = {0, 0, 0, 0xff00ff00u};
  v[3] = {0, length, 0, 0xff00ff00u};
  v[4] = {0, 0, 0, 0xffff0000u};
  v[5] = {0, 0, length, 0xffff0000u};

  bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                 BGFX_STATE_PT_LINES | BGFX_STATE_DEPTH_TEST_ALWAYS);
  bgfx::setTransform(model);
  bgfx::setVertexBuffer(0, &tvb);
  bgfx::submit(view_id, debug_program);
}

} // namespace engine
