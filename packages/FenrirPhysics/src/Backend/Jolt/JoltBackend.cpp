// PROTOTYPE (physics spike): the Jolt implementation of the backend seam. The only file that includes Jolt.

#include "Backend/Backend.hpp"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

JPH_SUPPRESS_WARNINGS

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace Fenrir::Backend
{
    namespace
    {
        // Two object layers and two broad-phase layers, as Jolt's HelloWorld. The real mapping comes from
        // CollisionFilter ("How do collision layers and filtering work?").
        constexpr JPH::ObjectLayer NonMoving = 0;
        constexpr JPH::ObjectLayer Moving = 1;

        class LayerInterface final : public JPH::BroadPhaseLayerInterface
        {
          public:
            JPH::uint GetNumBroadPhaseLayers() const override
            {
                return 2;
            }

            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
            {
                return JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(layer));
            }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
            {
                return static_cast<JPH::BroadPhaseLayer::Type>(layer) == 0 ? "NonMoving" : "Moving";
            }
#endif
        };

        class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
        {
          public:
            bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override
            {
                return layer == Moving || static_cast<JPH::BroadPhaseLayer::Type>(broadPhase) == Moving;
            }
        };

        class ObjectPairs final : public JPH::ObjectLayerPairFilter
        {
          public:
            bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
            {
                return a == Moving || b == Moving;
            }
        };

        void Trace(const char* format, ...)
        {
            va_list args;
            va_start(args, format);
            std::vfprintf(stderr, format, args);
            std::fputc('\n', stderr);
            va_end(args);
        }

#ifdef JPH_ENABLE_ASSERTS
        bool AssertFailed(const char* expression, const char* message, const char* file, JPH::uint line)
        {
            std::fprintf(stderr, "Jolt assert %s:%u: (%s) %s\n", file, line, expression, message ? message : "");
            return true; // break into the debugger
        }
#endif

        int s_users = 0;

        JPH::Vec3 ToJolt(const Math::Vec3& v)
        {
            return JPH::Vec3(v.x, v.y, v.z);
        }

        JPH::Quat ToJolt(const Math::Quat& q)
        {
            return JPH::Quat(q.x, q.y, q.z, q.w).Normalized();
        }

        Math::Vec3 FromJolt(JPH::Vec3Arg v)
        {
            return Math::Vec3(v.GetX(), v.GetY(), v.GetZ());
        }

        Math::Quat FromJolt(JPH::QuatArg q)
        {
            return Math::Quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
        }

        JPH::EMotionType ToJolt(MotionType type)
        {
            switch (type)
            {
            case MotionType::Static:
                return JPH::EMotionType::Static;
            case MotionType::Kinematic:
                return JPH::EMotionType::Kinematic;
            case MotionType::Dynamic:
                return JPH::EMotionType::Dynamic;
            }
            return JPH::EMotionType::Static;
        }

        // Scale is baked into the shape's dimensions (a new shape when it changes).
        JPH::Ref<JPH::Shape> MakeShape(const CollisionShape& shape, const Math::Vec3& scale)
        {
            const Math::Vec3 s = glm::abs(scale);
            switch (shape.Type())
            {
            case ShapeType::Box: {
                const Math::Vec3 half = shape.As<BoxShape>().halfExtents * s;
                const float minHalf = std::min({half.x, half.y, half.z});
                return new JPH::BoxShape(ToJolt(half), std::min(JPH::cDefaultConvexRadius, minHalf * 0.5f));
            }
            case ShapeType::Sphere:
                return new JPH::SphereShape(shape.As<SphereShape>().radius * std::max({s.x, s.y, s.z}));
            }
            return nullptr;
        }
    } // namespace

    void Acquire()
    {
        if (s_users++ > 0)
        {
            return;
        }
        JPH::RegisterDefaultAllocator();
        JPH::Trace = Trace;
        JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailed;)
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }

    void Release()
    {
        if (--s_users > 0)
        {
            return;
        }
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }

    struct World::Impl
    {
        LayerInterface layers;
        ObjectVsBroadPhase objectVsBroadPhase;
        ObjectPairs objectPairs;
        JPH::TempAllocatorImpl tempAllocator{10 * 1024 * 1024};
        JPH::JobSystemSingleThreaded jobs{JPH::cMaxPhysicsJobs};
        JPH::PhysicsSystem system;

        JPH::BodyInterface& Bodies()
        {
            return system.GetBodyInterfaceNoLock();
        }
    };

    World::World(const WorldSettings& settings) : m_impl(std::make_unique<Impl>())
    {
        m_impl->system.Init(settings.maxBodies, 0, settings.maxBodyPairs, settings.maxContacts, m_impl->layers,
                            m_impl->objectVsBroadPhase, m_impl->objectPairs);
        m_impl->system.SetGravity(ToJolt(settings.gravity));
    }

    World::~World() = default;

    BodyId World::CreateBody(const BodyDesc& desc)
    {
        const bool moving = desc.motionType != MotionType::Static;
        JPH::BodyCreationSettings settings(MakeShape(desc.shape, desc.scale), ToJolt(desc.position),
                                           ToJolt(desc.rotation), ToJolt(desc.motionType), moving ? Moving : NonMoving);
        settings.mAllowDynamicOrKinematic = moving;
        settings.mUserData = desc.userData;
        settings.mLinearVelocity = ToJolt(desc.linearVelocity);
        settings.mAngularVelocity = ToJolt(desc.angularVelocity);
        if (desc.motionType == MotionType::Dynamic)
        {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = desc.mass;
        }

        const JPH::BodyID id = m_impl->Bodies().CreateAndAddBody(settings, moving ? JPH::EActivation::Activate
                                                                                  : JPH::EActivation::DontActivate);
        return BodyId{id.GetIndexAndSequenceNumber()};
    }

    void World::DestroyBody(BodyId id)
    {
        const JPH::BodyID body(id.value);
        m_impl->Bodies().RemoveBody(body);
        m_impl->Bodies().DestroyBody(body);
    }

    void World::Teleport(BodyId id, const Math::Vec3& position, const Math::Quat& rotation)
    {
        m_impl->Bodies().SetPositionAndRotation(JPH::BodyID(id.value), ToJolt(position), ToJolt(rotation),
                                                JPH::EActivation::Activate);
    }

    void World::MoveKinematic(BodyId id, const Math::Vec3& position, const Math::Quat& rotation, float dt)
    {
        m_impl->Bodies().MoveKinematic(JPH::BodyID(id.value), ToJolt(position), ToJolt(rotation), dt);
    }

    void World::SetVelocity(BodyId id, const Math::Vec3& linear, const Math::Vec3& angular)
    {
        m_impl->Bodies().SetLinearAndAngularVelocity(JPH::BodyID(id.value), ToJolt(linear), ToJolt(angular));
    }

    void World::SetShape(BodyId id, const CollisionShape& shape, const Math::Vec3& scale)
    {
        m_impl->Bodies().SetShape(JPH::BodyID(id.value), MakeShape(shape, scale), true, JPH::EActivation::Activate);
    }

    BodyState World::GetState(BodyId id) const
    {
        const JPH::BodyID body(id.value);
        JPH::RVec3 position;
        JPH::Quat rotation;
        m_impl->Bodies().GetPositionAndRotation(body, position, rotation);
        return BodyState{FromJolt(position), FromJolt(rotation), FromJolt(m_impl->Bodies().GetLinearVelocity(body)),
                         FromJolt(m_impl->Bodies().GetAngularVelocity(body))};
    }

    void World::SetGravity(const Math::Vec3& gravity)
    {
        m_impl->system.SetGravity(ToJolt(gravity));
    }

    void World::Step(float dt, int collisionSteps)
    {
        const JPH::EPhysicsUpdateError error =
            m_impl->system.Update(dt, collisionSteps, &m_impl->tempAllocator, &m_impl->jobs);
        if (error != JPH::EPhysicsUpdateError::None)
        {
            Trace("Physics step overflowed a capacity (EPhysicsUpdateError %u)", static_cast<unsigned>(error));
        }
    }
} // namespace Fenrir::Backend
