#include "engine/model.hpp"

// cgltf implementation lives in rigged_model.cpp — include header only here.
#include "cgltf.h"

#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/math.h>

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

namespace {

struct StaticVertex {
    float    x = 0.0f, y = 0.0f, z = 0.0f;
    float    nx = 0.0f, ny = 1.0f, nz = 0.0f;
    float    u = 0.0f,  v = 0.0f;
    uint32_t abgr = 0xffffffffu;
};

struct Primitive {
    bgfx::VertexBufferHandle vb     = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle  ib     = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle      albedo = BGFX_INVALID_HANDLE;
};

// bx::mtxMul(out, a, b) computes out = b*a. We use math notation out = A*B.
inline void mat_mul(float out[16], const float a[16], const float b[16])
{
    bx::mtxMul(out, b, a);
}

bgfx::TextureHandle decode_texture(const uint8_t* data, size_t size)
{
    if (!data || size == 0) return BGFX_INVALID_HANDLE;
    bx::DefaultAllocator alloc;
    bimg::ImageContainer* img = bimg::imageParse(&alloc, data,
        static_cast<uint32_t>(size), bimg::TextureFormat::RGBA8);
    if (!img) return BGFX_INVALID_HANDLE;
    if (img->m_cubeMap || img->m_depth > 1 || !img->m_data) {
        bimg::imageFree(img); return BGFX_INVALID_HANDLE;
    }
    const bgfx::Memory* mem = bgfx::copy(img->m_data, img->m_size);
    bgfx::TextureHandle h = bgfx::createTexture2D(
        static_cast<uint16_t>(img->m_width),
        static_cast<uint16_t>(img->m_height),
        img->m_numMips > 1,
        static_cast<uint16_t>(img->m_numLayers),
        static_cast<bgfx::TextureFormat::Enum>(img->m_format),
        0, mem);
    bimg::imageFree(img);
    return h;
}

} // namespace

struct Model::State {
    bgfx::VertexLayout               layout;
    std::vector<Primitive>           primitives;
    std::vector<bgfx::TextureHandle> owned_textures;
    bool                             loaded = false;
};

Model::Model() : s_(new State())
{
    s_->layout.begin()
        .add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal,    3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true)
        .end();
}

Model::~Model()
{
    unload();
    delete s_;
    s_ = nullptr;
}

void Model::unload()
{
    for (auto& p : s_->primitives) {
        if (bgfx::isValid(p.vb)) bgfx::destroy(p.vb);
        if (bgfx::isValid(p.ib)) bgfx::destroy(p.ib);
    }
    for (auto& t : s_->owned_textures)
        if (bgfx::isValid(t)) bgfx::destroy(t);
    s_->primitives.clear();
    s_->owned_textures.clear();
    s_->loaded = false;
}

bool Model::valid() const { return s_->loaded; }

bool Model::load(const char* glb_path, std::string& err)
{
    cgltf_options opts{};
    cgltf_data*   data = nullptr;
    if (cgltf_parse_file(&opts, glb_path, &data) != cgltf_result_success) {
        err = std::string("cgltf_parse_file failed: ") + glb_path;
        return false;
    }
    if (cgltf_load_buffers(&opts, data, glb_path) != cgltf_result_success) {
        cgltf_free(data);
        err = "cgltf_load_buffers failed";
        return false;
    }

    std::unordered_map<size_t, bgfx::TextureHandle> image_cache;
    auto get_texture = [&](const cgltf_image* img) -> bgfx::TextureHandle {
        if (!img) return BGFX_INVALID_HANDLE;
        size_t idx = static_cast<size_t>(img - data->images);
        auto it = image_cache.find(idx);
        if (it != image_cache.end()) return it->second;
        if (!img->buffer_view || !img->buffer_view->buffer || !img->buffer_view->buffer->data) {
            image_cache[idx] = BGFX_INVALID_HANDLE;
            return BGFX_INVALID_HANDLE;
        }
        const uint8_t* base = static_cast<const uint8_t*>(img->buffer_view->buffer->data)
                              + img->buffer_view->offset;
        bgfx::TextureHandle h = decode_texture(base, img->buffer_view->size);
        image_cache[idx] = h;
        if (bgfx::isValid(h)) s_->owned_textures.push_back(h);
        return h;
    };

    for (size_t mi = 0; mi < data->meshes_count; ++mi) {
        const cgltf_mesh& mesh = data->meshes[mi];
        for (size_t pi = 0; pi < mesh.primitives_count; ++pi) {
            const cgltf_primitive& prim = mesh.primitives[pi];
            const cgltf_accessor *accPos=nullptr, *accNrm=nullptr, *accUv=nullptr, *accCol=nullptr;
            for (size_t ai = 0; ai < prim.attributes_count; ++ai) {
                const cgltf_attribute& a = prim.attributes[ai];
                switch (a.type) {
                    case cgltf_attribute_type_position: accPos = a.data; break;
                    case cgltf_attribute_type_normal:   accNrm = a.data; break;
                    case cgltf_attribute_type_texcoord: if (a.index == 0) accUv  = a.data; break;
                    case cgltf_attribute_type_color:    if (a.index == 0) accCol = a.data; break;
                    default: break;
                }
            }
            if (!accPos) continue;

            const size_t vcount = accPos->count;
            std::vector<StaticVertex> verts(vcount);
            for (size_t i = 0; i < vcount; ++i) {
                StaticVertex& sv = verts[i];
                float p[3]={0,0,0}; cgltf_accessor_read_float(accPos, i, p, 3);
                sv.x=p[0]; sv.y=p[1]; sv.z=p[2];
                if (accNrm) { float n[3]={0,1,0}; cgltf_accessor_read_float(accNrm, i, n, 3); sv.nx=n[0]; sv.ny=n[1]; sv.nz=n[2]; }
                if (accUv)  { float uv[2]={0,0};  cgltf_accessor_read_float(accUv,  i, uv, 2); sv.u=uv[0]; sv.v=uv[1]; }
                if (accCol) {
                    float c[4]={1,1,1,1}; cgltf_accessor_read_float(accCol, i, c, 4);
                    uint8_t r = static_cast<uint8_t>(c[0]*255.0f + 0.5f);
                    uint8_t g = static_cast<uint8_t>(c[1]*255.0f + 0.5f);
                    uint8_t b = static_cast<uint8_t>(c[2]*255.0f + 0.5f);
                    uint8_t a = static_cast<uint8_t>(c[3]*255.0f + 0.5f);
                    sv.abgr = (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16)
                            | (static_cast<uint32_t>(g) << 8)  |  static_cast<uint32_t>(r);
                }
            }

            Primitive p;
            p.vb = bgfx::createVertexBuffer(
                bgfx::copy(verts.data(), uint32_t(vcount * sizeof(StaticVertex))), s_->layout);

            if (prim.indices) {
                const cgltf_accessor* accIdx = prim.indices;
                const size_t icount = accIdx->count;
                if (icount > 0xFFFF || accIdx->component_type == cgltf_component_type_r_32u) {
                    std::vector<uint32_t> idx(icount);
                    for (size_t i=0; i<icount; ++i) { cgltf_uint v=0; cgltf_accessor_read_uint(accIdx,i,&v,1); idx[i]=v; }
                    p.ib = bgfx::createIndexBuffer(bgfx::copy(idx.data(), uint32_t(icount*4)), BGFX_BUFFER_INDEX32);
                } else {
                    std::vector<uint16_t> idx(icount);
                    for (size_t i=0; i<icount; ++i) { cgltf_uint v=0; cgltf_accessor_read_uint(accIdx,i,&v,1); idx[i]=uint16_t(v); }
                    p.ib = bgfx::createIndexBuffer(bgfx::copy(idx.data(), uint32_t(icount*2)));
                }
            }

            if (prim.material && prim.material->has_pbr_metallic_roughness) {
                const cgltf_pbr_metallic_roughness& pbr = prim.material->pbr_metallic_roughness;
                if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image)
                    p.albedo = get_texture(pbr.base_color_texture.texture->image);
            }
            s_->primitives.push_back(p);
        }
    }

    cgltf_free(data);
    s_->loaded = true;
    return true;
}

void Model::submit_world(
    bgfx::ViewId view_id, bgfx::ProgramHandle program,
    bgfx::UniformHandle s_albedo, bgfx::UniformHandle s_normal, bgfx::UniformHandle s_roughness,
    bgfx::TextureHandle fallback_albedo, bgfx::TextureHandle fallback_normal, bgfx::TextureHandle fallback_roughness,
    uint64_t state, const ModelDrawParams& p)
{
    if (!s_->loaded) return;

    float scl[16], rot[16], rs[16], t[16], model[16];
    bx::mtxScale(scl, p.scale, p.scale, p.scale);
    bx::mtxRotateY(rot, p.yaw);
    mat_mul(rs, rot, scl);
    bx::mtxTranslate(t, p.pos[0], p.pos[1], p.pos[2]);
    mat_mul(model, t, rs);

    for (const Primitive& prim : s_->primitives) {
        if (!bgfx::isValid(prim.vb)) continue;
        bgfx::setState(state);
        bgfx::setTransform(model);
        bgfx::setTexture(0, s_albedo,    bgfx::isValid(prim.albedo) ? prim.albedo : fallback_albedo);
        bgfx::setTexture(1, s_normal,    fallback_normal);
        bgfx::setTexture(2, s_roughness, fallback_roughness);
        bgfx::setVertexBuffer(0, prim.vb);
        if (bgfx::isValid(prim.ib)) bgfx::setIndexBuffer(prim.ib);
        bgfx::submit(view_id, program);
    }
}

} // namespace engine
