// Jolt headers must be included before any other project headers that might define conflicting macros.
// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/PhysicsSystem.h>
// clang-format on

#include "engine/physics/jolt_physics.hpp"
#include "engine/lit_vertex.hpp"
#include "game/level/level_mesh.hpp"

#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>

namespace engine {

// ---------------------------------------------------------------------------
// Layer constants
// ---------------------------------------------------------------------------
namespace Layers {
    static constexpr JPH::ObjectLayer STATIC  = 0;
    static constexpr JPH::ObjectLayer MOVING  = 1;
    static constexpr JPH::ObjectLayer NUM     = 2;
}
namespace BPLayers {
    static constexpr JPH::BroadPhaseLayer STATIC{0};
    static constexpr JPH::BroadPhaseLayer MOVING{1};
    static constexpr uint32_t NUM = 2;
}

// ---------------------------------------------------------------------------
// Filter implementations
// ---------------------------------------------------------------------------
class BPLayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
    BPLayerInterface() {
        mObjectToBroadPhase[Layers::STATIC] = BPLayers::STATIC;
        mObjectToBroadPhase[Layers::MOVING] = BPLayers::MOVING;
    }
    uint32_t GetNumBroadPhaseLayers() const override { return BPLayers::NUM; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer l) const override {
        return mObjectToBroadPhase[l];
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer l) const override {
        return l == BPLayers::STATIC ? "STATIC" : "MOVING";
    }
#endif
private:
    JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM]{};
};

class ObjVsBPFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer l1, JPH::BroadPhaseLayer l2) const override {
        switch (l1) {
        case Layers::STATIC: return false;                  // static never moves into other bodies
        case Layers::MOVING: return l2 == BPLayers::STATIC; // characters collide with level only
        default: return false;
        }
    }
};

class ObjVsObjFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer l1, JPH::ObjectLayer l2) const override {
        switch (l1) {
        case Layers::STATIC: return false;
        case Layers::MOVING: return l2 == Layers::STATIC;
        default: return false;
        }
    }
};

// Filter that only allows static-layer bodies (used for character movement and raycasts).
class StaticOnlyLayerFilter final : public JPH::ObjectLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer l) const override { return l == Layers::STATIC; }
};

class StaticOnlyBPFilter final : public JPH::BroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::BroadPhaseLayer l) const override { return l == BPLayers::STATIC; }
};

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
static constexpr float k_gravity      = 28.f;
static constexpr float k_char_padding = 0.02f;

struct CharEntry {
    JPH::Ref<JPH::CharacterVirtual> character;
    float step_height = 0.4f;
    bool  active      = true;
};

struct JoltPhysics::Impl {
    BPLayerInterface   bp_interface;
    ObjVsBPFilter      obj_vs_bp;
    ObjVsObjFilter     obj_vs_obj;

    JPH::TempAllocatorImpl    temp_allocator{8 * 1024 * 1024};
    JPH::JobSystemThreadPool  job_system;
    JPH::PhysicsSystem        physics_system;

    JPH::BodyID level_body_id{JPH::BodyID::cInvalidBodyID};

    std::vector<CharEntry>  chars;
    std::vector<JPH::BodyID> kinematic_bodies;
};

// ---------------------------------------------------------------------------
// JoltPhysics
// ---------------------------------------------------------------------------
JoltPhysics::JoltPhysics()  = default;
JoltPhysics::~JoltPhysics() = default;

bool JoltPhysics::init() {
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    impl_ = std::make_unique<Impl>();

    const int num_threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    impl_->job_system.Init(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, num_threads);

    constexpr uint32_t k_max_bodies       = 1024;
    constexpr uint32_t k_num_body_mutexes = 0; // auto
    constexpr uint32_t k_max_body_pairs   = 1024;
    constexpr uint32_t k_max_contact_cst  = 1024;

    impl_->physics_system.Init(
        k_max_bodies, k_num_body_mutexes, k_max_body_pairs, k_max_contact_cst,
        impl_->bp_interface, impl_->obj_vs_bp, impl_->obj_vs_obj);

    impl_->physics_system.SetGravity(JPH::Vec3(0.f, -k_gravity, 0.f));

    return true;
}

void JoltPhysics::shutdown() {
    if (!impl_) return;

    impl_->chars.clear();

    JPH::BodyInterface& bi = impl_->physics_system.GetBodyInterface();
    for (JPH::BodyID& id : impl_->kinematic_bodies) {
        if (!id.IsInvalid()) {
            bi.RemoveBody(id);
            bi.DestroyBody(id);
            id = JPH::BodyID{JPH::BodyID::cInvalidBodyID};
        }
    }
    impl_->kinematic_bodies.clear();

    if (!impl_->level_body_id.IsInvalid()) {
        bi.RemoveBody(impl_->level_body_id);
        bi.DestroyBody(impl_->level_body_id);
        impl_->level_body_id = JPH::BodyID{JPH::BodyID::cInvalidBodyID};
    }

    impl_.reset();

    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}

void JoltPhysics::build_level(const engine::LevelMeshOutput& mesh) {
    // Remove old level body if any.
    JPH::BodyInterface& bi = impl_->physics_system.GetBodyInterface();
    if (!impl_->level_body_id.IsInvalid()) {
        bi.RemoveBody(impl_->level_body_id);
        bi.DestroyBody(impl_->level_body_id);
        impl_->level_body_id = JPH::BodyID{JPH::BodyID::cInvalidBodyID};
    }

    // Gather all triangles from floor + wall + stair vertex arrays.
    // Each array is a flat triangle list (3 verts per triangle, no index buffer).
    JPH::TriangleList triangles;

    auto add_triangles = [&](const std::vector<LitVertex>& verts) {
        const size_t n = verts.size();
        for (size_t i = 0; i + 2 < n; i += 3) {
            triangles.push_back(JPH::Triangle{
                JPH::Float3{verts[i+0].x, verts[i+0].y, verts[i+0].z},
                JPH::Float3{verts[i+1].x, verts[i+1].y, verts[i+1].z},
                JPH::Float3{verts[i+2].x, verts[i+2].y, verts[i+2].z},
                0 // material index
            });
        }
    };
    add_triangles(mesh.floor_vertices);
    add_triangles(mesh.wall_vertices);
    add_triangles(mesh.stair_vertices);

    if (triangles.empty()) return;

    JPH::MeshShapeSettings mesh_settings{triangles};
    mesh_settings.mActiveEdgeCosThresholdAngle = 0.f;
    auto result = mesh_settings.Create();
    if (result.HasError()) return;

    JPH::BodyCreationSettings bcs{
        result.Get(), JPH::RVec3::sZero(), JPH::Quat::sIdentity(),
        JPH::EMotionType::Static, Layers::STATIC
    };
    impl_->level_body_id = bi.CreateAndAddBody(bcs, JPH::EActivation::DontActivate);
}

CharHandle JoltPhysics::add_character(float x, float y, float z,
                                       float radius, float height, float step_height) {
    const float half_shaft = (height - 2.f * radius) * 0.5f;
    if (half_shaft < 0.f) return k_invalid_char;

    // ShapeSettings inherit from RefTarget — must be heap-allocated so that
    // RotatedTranslatedShapeSettings can safely store and release a Ref<> to them.
    JPH::Ref<JPH::CapsuleShapeSettings> cap_settings = new JPH::CapsuleShapeSettings{half_shaft, radius};
    cap_settings->mMaterial = JPH::PhysicsMaterial::sDefault;

    // Offset so the bottom of the capsule sits at the character's origin (feet).
    const float offset_y = half_shaft + radius;
    JPH::Ref<JPH::RotatedTranslatedShapeSettings> rts = new JPH::RotatedTranslatedShapeSettings{
        JPH::Vec3{0.f, offset_y, 0.f},
        JPH::Quat::sIdentity(),
        cap_settings
    };
    auto shape_res = rts->Create();
    if (shape_res.HasError()) return k_invalid_char;

    // CharacterBaseSettings also inherits from RefTarget — heap-allocate.
    JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings{};
    settings->mShape             = shape_res.Get();
    settings->mMaxSlopeAngle     = JPH::DegreesToRadians(50.f);
    settings->mMaxStrength       = 100.f;
    settings->mCharacterPadding  = k_char_padding;
    settings->mSupportingVolume  = JPH::Plane{JPH::Vec3::sAxisY(), -radius};
    settings->mBackFaceMode      = JPH::EBackFaceMode::CollideWithBackFaces;
    settings->mMass              = 70.f;

    JPH::Ref<JPH::CharacterVirtual> character = new JPH::CharacterVirtual(
        settings,
        JPH::RVec3{x, y, z},
        JPH::Quat::sIdentity(),
        0,
        &impl_->physics_system
    );

    // Find a free slot or push a new one.
    for (uint32_t i = 0; i < static_cast<uint32_t>(impl_->chars.size()); ++i) {
        if (!impl_->chars[i].active) {
            impl_->chars[i] = CharEntry{character, step_height, true};
            return i;
        }
    }
    impl_->chars.push_back(CharEntry{character, step_height, true});
    return static_cast<CharHandle>(impl_->chars.size() - 1);
}

void JoltPhysics::remove_character(CharHandle h) {
    if (h >= impl_->chars.size()) return;
    impl_->chars[h].character = nullptr;
    impl_->chars[h].active    = false;
}

void JoltPhysics::move_character(CharHandle h, CharVerticalState& vs,
                                  float vel_x, float vel_z, float dt) {
    if (h >= impl_->chars.size() || !impl_->chars[h].active) return;
    auto& entry = impl_->chars[h];
    auto& ch    = *entry.character;

    // Gravity: reset when standing on ground, accumulate when airborne.
    const bool on_ground = ch.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
    if (on_ground && vs.velocity_y <= 0.f) {
        vs.velocity_y = 0.f;
    } else {
        vs.velocity_y -= k_gravity * dt;
        constexpr float k_terminal = -50.f;
        if (vs.velocity_y < k_terminal) vs.velocity_y = k_terminal;
    }

    ch.SetLinearVelocity(JPH::Vec3{vel_x, vs.velocity_y, vel_z});

    JPH::CharacterVirtual::ExtendedUpdateSettings eu;
    eu.mStickToFloorStepDown = JPH::Vec3{0.f, -0.5f, 0.f};
    eu.mWalkStairsStepUp     = JPH::Vec3{0.f, entry.step_height + k_char_padding, 0.f};

    static const StaticOnlyLayerFilter s_layer_filter;
    static const StaticOnlyBPFilter    s_bp_filter;

    ch.ExtendedUpdate(dt,
        JPH::Vec3{0.f, -k_gravity, 0.f},
        eu,
        s_bp_filter,
        s_layer_filter,
        {},
        {},
        impl_->temp_allocator);
}

void JoltPhysics::get_character_pos(CharHandle h, float& x, float& y, float& z) const {
    if (h >= impl_->chars.size() || !impl_->chars[h].active) {
        x = y = z = 0.f; return;
    }
    const JPH::RVec3 p = impl_->chars[h].character->GetPosition();
    x = p.GetX(); y = p.GetY(); z = p.GetZ();
}

void JoltPhysics::set_character_pos(CharHandle h, float x, float y, float z) {
    if (h >= impl_->chars.size() || !impl_->chars[h].active) return;
    impl_->chars[h].character->SetPosition(JPH::RVec3{x, y, z});
}

BodyHandle JoltPhysics::add_kinematic_box(
    float x, float y, float z, float angle_y,
    float half_w, float half_h, float half_d,
    float cx, float cy, float cz)
{
    JPH::Ref<JPH::BoxShapeSettings> box = new JPH::BoxShapeSettings{
        JPH::Vec3{half_w, half_h, half_d}
    };
    JPH::Ref<JPH::RotatedTranslatedShapeSettings> rts = new JPH::RotatedTranslatedShapeSettings{
        JPH::Vec3{cx, cy, cz}, JPH::Quat::sIdentity(), box
    };
    auto result = rts->Create();
    if (result.HasError()) return k_invalid_body;

    const JPH::Quat rot = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), angle_y);
    JPH::BodyCreationSettings bcs{
        result.Get(), JPH::RVec3{x, y, z}, rot,
        JPH::EMotionType::Kinematic, Layers::STATIC
    };
    JPH::BodyInterface& bi = impl_->physics_system.GetBodyInterface();
    JPH::BodyID id = bi.CreateAndAddBody(bcs, JPH::EActivation::Activate);
    if (id.IsInvalid()) return k_invalid_body;

    for (uint32_t i = 0; i < static_cast<uint32_t>(impl_->kinematic_bodies.size()); ++i) {
        if (impl_->kinematic_bodies[i].IsInvalid()) {
            impl_->kinematic_bodies[i] = id;
            return i;
        }
    }
    impl_->kinematic_bodies.push_back(id);
    return static_cast<BodyHandle>(impl_->kinematic_bodies.size() - 1);
}

void JoltPhysics::set_kinematic_transform(BodyHandle h, float x, float y, float z, float angle_y) {
    if (h >= static_cast<uint32_t>(impl_->kinematic_bodies.size())) return;
    const JPH::BodyID id = impl_->kinematic_bodies[h];
    if (id.IsInvalid()) return;
    JPH::BodyInterface& bi = impl_->physics_system.GetBodyInterface();
    const JPH::Quat rot = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), angle_y);
    bi.SetPositionAndRotation(id, JPH::RVec3{x, y, z}, rot, JPH::EActivation::DontActivate);
}

void JoltPhysics::remove_body(BodyHandle h) {
    if (h >= static_cast<uint32_t>(impl_->kinematic_bodies.size())) return;
    JPH::BodyID& id = impl_->kinematic_bodies[h];
    if (id.IsInvalid()) return;
    JPH::BodyInterface& bi = impl_->physics_system.GetBodyInterface();
    bi.RemoveBody(id);
    bi.DestroyBody(id);
    id = JPH::BodyID{JPH::BodyID::cInvalidBodyID};
}

bool JoltPhysics::cast_ray_level(float ox, float oy, float oz,
                                  float dx, float dy, float dz,
                                  float max_dist,
                                  JoltRayHit& hit) const {
    JPH::RRayCast ray{
        JPH::RVec3{ox, oy, oz},
        JPH::Vec3{dx * max_dist, dy * max_dist, dz * max_dist}
    };

    JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> collector;

    static const StaticOnlyBPFilter    s_bp_filter;
    static const StaticOnlyLayerFilter s_layer_filter;

    JPH::RayCastSettings ray_settings;
    ray_settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
    impl_->physics_system.GetNarrowPhaseQuery().CastRay(
        ray, ray_settings, collector, s_bp_filter, s_layer_filter);

    if (!collector.HadHit()) return false;

    const auto& h_result = collector.mHit;
    const float t = h_result.mFraction * max_dist;

    // Get surface normal from the body.
    JPH::BodyLockRead lock(impl_->physics_system.GetBodyLockInterface(), h_result.mBodyID);
    if (!lock.Succeeded()) return false;

    const JPH::Body& body    = lock.GetBody();
    const JPH::Vec3  hit_pos = JPH::Vec3(JPH::RVec3{ox, oy, oz}) + JPH::Vec3{dx, dy, dz} * t;
    JPH::Vec3        normal  = body.GetWorldSpaceSurfaceNormal(h_result.mSubShapeID2, hit_pos);

    // Ensure normal points toward the ray origin (outward from the surface).
    if (normal.Dot(-JPH::Vec3{dx, dy, dz}) < 0.f)
        normal = -normal;

    hit.t  = t;
    hit.nx = normal.GetX();
    hit.ny = normal.GetY();
    hit.nz = normal.GetZ();
    return true;
}

void JoltPhysics::update(float dt) {
    constexpr int k_steps = 1;
    impl_->physics_system.Update(dt, k_steps, &impl_->temp_allocator, &impl_->job_system);
}

} // namespace engine
