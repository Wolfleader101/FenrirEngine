// The Jolt implementation of the physics backend seam (ADR-0010). The only code that includes Jolt.

#include "Backend/Backend.hpp"

#include <algorithm>
#include <bit>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "FenrirLogger/ILogger.hpp"

#include <Jolt/Jolt.h>

JPH_SUPPRESS_WARNING_PUSH
JPH_SUPPRESS_WARNINGS
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
JPH_SUPPRESS_WARNING_POP

namespace Fenrir::Backend
{
    namespace
    {
        // Capacities of every world. Plain limits a later change takes from PhysicsConfig.
        constexpr JPH::uint maxBodies = 65536;
        constexpr JPH::uint maxBodyPairs = 65536;
        constexpr JPH::uint maxContactConstraints = 10240;
        constexpr std::size_t tempAllocatorBytes = 10 * 1024 * 1024;
        constexpr float wakeMargin = 0.1f;

        // Static bodies in one object layer, moving ones in another; static bodies never collide with each other.
        // Collision layers and masks replace this with their own mapping.
        constexpr JPH::ObjectLayer staticLayer = 0;
        constexpr JPH::ObjectLayer movingLayer = 1;
        constexpr JPH::BroadPhaseLayer staticBroadPhase(0);
        constexpr JPH::BroadPhaseLayer movingBroadPhase(1);

        class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
        {
          public:
            JPH::uint GetNumBroadPhaseLayers() const override
            {
                return 2;
            }

            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
            {
                return layer == staticLayer ? staticBroadPhase : movingBroadPhase;
            }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
            {
                return layer == staticBroadPhase ? "Static" : "Moving";
            }
#endif
        };

        class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
        {
          public:
            bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override
            {
                return layer == movingLayer || broadPhase == movingBroadPhase;
            }
        };

        class ObjectPairs final : public JPH::ObjectLayerPairFilter
        {
          public:
            bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
            {
                return a == movingLayer || b == movingLayer;
            }
        };

        // Jolt reports through global functions with no user data, so its messages go to the logger of the newest
        // Context still alive.
        std::vector<ILogger*> s_loggers;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral" // Jolt's messages are printf formats it passes on
#endif
        std::string Format(const char* format, va_list args)
        {
            va_list counting;
            va_copy(counting, args);
            const int length = std::vsnprintf(nullptr, 0, format, counting);
            va_end(counting);
            if (length <= 0)
            {
                return {};
            }
            std::string text(static_cast<std::size_t>(length) + 1, '\0');
            std::vsnprintf(text.data(), text.size(), format, args);
            text.resize(static_cast<std::size_t>(length));
            return text;
        }
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

        void Trace(const char* format, ...)
        {
            va_list args;
            va_start(args, format);
            const std::string message = "Physics backend: " + Format(format, args);
            va_end(args);
            if (!s_loggers.empty())
            {
                s_loggers.back()->Warn(message);
            }
        }

#ifdef JPH_ENABLE_ASSERTS
        bool AssertFailed(const char* expression, const char* message, const char* file, JPH::uint line)
        {
            const std::string text = "Physics backend assertion failed: " + std::string(expression) + " (" +
                                     (message != nullptr ? message : "") + ") at " + file + ":" + std::to_string(line);
            if (!s_loggers.empty())
            {
                s_loggers.back()->Error(text);
            }
            else
            {
                std::fprintf(stderr, "%s\n", text.c_str());
            }
            return true; // break: a backend assertion is a FenrirPhysics bug
        }
#endif

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
            if (type == MotionType::Kinematic)
            {
                return JPH::EMotionType::Kinematic;
            }
            return type == MotionType::Dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static;
        }

        JPH::ObjectLayer LayerOf(MotionType type)
        {
            return type == MotionType::Static ? staticLayer : movingLayer;
        }

        // No dimension of a box, sphere or capsule is made smaller than this, after scale: the backend asserts on
        // empty shapes, and anything thinner than a millimetre is not simulated well anyway.
        constexpr float minimumSize = 1.0e-3f;

        // A box, sphere or capsule with the entity's scale baked into its dimensions: what the backend shape is made
        // from, and the key of the shape cache. Box: half extents. Sphere: radius in x. Capsule: half height in x,
        // radius in y. Unused dimensions are zero.
        struct Primitive
        {
            ShapeType type;
            Math::Vec3 size;

            // Ordered bit for bit, like the diff: equal keys make equal shapes.
            friend bool operator<(const Primitive& a, const Primitive& b)
            {
                const auto key = [](const Primitive& p) {
                    return std::tuple(p.type, std::bit_cast<std::uint32_t>(p.size.x),
                                      std::bit_cast<std::uint32_t>(p.size.y), std::bit_cast<std::uint32_t>(p.size.z));
                };
                return key(a) < key(b);
            }
        };

        // Mirroring is ignored (a primitive is symmetric) and no dimension is below minimumSize.
        Primitive Bake(const CollisionShape& shape, const Math::Vec3& scale)
        {
            const Math::Vec3 size = glm::abs(scale);
            if (shape.Is<SphereShape>())
            {
                // Jolt's sphere takes only a uniform scale: the largest axis wins.
                const float radius = shape.As<SphereShape>().radius * std::max({size.x, size.y, size.z});
                return Primitive{ShapeType::Sphere, Math::Vec3(std::max(radius, minimumSize), 0.0f, 0.0f)};
            }
            if (shape.Is<CapsuleShape>())
            {
                // Jolt's capsule takes only a uniform scale: its height follows y, its radius the larger of x and z.
                const CapsuleShape& capsule = shape.As<CapsuleShape>();
                return Primitive{ShapeType::Capsule,
                                 Math::Vec3(std::max(capsule.halfHeight * size.y, minimumSize),
                                            std::max(capsule.radius * std::max(size.x, size.z), minimumSize), 0.0f)};
            }
            return Primitive{ShapeType::Box,
                             glm::max(shape.As<BoxShape>().halfExtents * size, Math::Vec3(minimumSize))};
        }

        JPH::Ref<JPH::Shape> Make(const Primitive& primitive)
        {
            const Math::Vec3& size = primitive.size;
            if (primitive.type == ShapeType::Sphere)
            {
                return new JPH::SphereShape(size.x);
            }
            if (primitive.type == ShapeType::Capsule)
            {
                return new JPH::CapsuleShape(size.x, size.y);
            }
            // Jolt rounds a box's edges by a convex radius, which must fit inside the box: thin boxes get a smaller
            // one.
            const float convexRadius = std::min(JPH::cDefaultConvexRadius, 0.5f * std::min({size.x, size.y, size.z}));
            return new JPH::BoxShape(ToJolt(size), convexRadius);
        }

        CollisionMesh MeshOf(const CollisionShape& shape)
        {
            return shape.Is<ConvexHullShape>() ? shape.As<ConvexHullShape>().hull : shape.As<MeshShape>().mesh;
        }

        bool IsPrimitive(const CollisionShape& shape)
        {
            return !shape.Is<ConvexHullShape>() && !shape.Is<MeshShape>();
        }

        // A body's mass properties: the shape's own, scaled to `mass`. A triangle mesh has no volume, so it is weighed
        // as the solid box of its bounds.
        JPH::MassProperties MassOf(const JPH::Shape& shape, float mass)
        {
            JPH::MassProperties properties = shape.GetMassProperties();
            if (properties.mMass <= 0.0f)
            {
                const JPH::Vec3 size =
                    JPH::Vec3::sMax(shape.GetLocalBounds().GetSize(), JPH::Vec3::sReplicate(2.0f * minimumSize));
                properties.SetMassAndInertiaOfSolidBox(size, 1.0f);
            }
            properties.ScaleToMass(mass);
            return properties;
        }

        JPH::BodyID ToJolt(BodyId id)
        {
            return JPH::BodyID(id.value);
        }
    } // namespace

    Context::Context(ILogger& logger) : m_logger(&logger)
    {
        if (s_loggers.empty())
        {
            JPH::RegisterDefaultAllocator();
            JPH::Trace = Trace;
            JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailed;)
            JPH::Factory::sInstance = new JPH::Factory();
            JPH::RegisterTypes();
        }
        s_loggers.push_back(m_logger);
    }

    Context::~Context()
    {
        // Contexts may end in any order: remove this one's logger, newest first.
        const auto it = std::find(s_loggers.rbegin(), s_loggers.rend(), m_logger);
        if (it != s_loggers.rend())
        {
            s_loggers.erase(std::next(it).base());
        }
        if (s_loggers.empty())
        {
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }
    }

    struct CollisionMeshes::Impl
    {
        // A mesh's source data and the shapes cooked from it so far. The source is dropped once both kinds are cooked.
        struct Entry
        {
            JPH::Array<JPH::Vec3> vertices;
            JPH::IndexedTriangleList triangles;
            JPH::Ref<JPH::Shape> hull;
            JPH::Ref<JPH::Shape> mesh;
            bool hullFailed = false;
            bool meshFailed = false;
        };

        explicit Impl(ILogger& meshesLogger) : logger(&meshesLogger)
        {
        }

        // The shape a hull or mesh refers to, cooked the first time; null if the mesh is released or cannot be cooked
        // as that kind (an error the first time).
        const JPH::Shape* Shape(const CollisionShape& shape)
        {
            const CollisionMesh handle = MeshOf(shape);
            const auto it = meshes.find(handle.id);
            if (it == meshes.end())
            {
                return nullptr;
            }
            Entry& entry = it->second;
            const bool hull = shape.Is<ConvexHullShape>();
            JPH::Ref<JPH::Shape>& cooked = hull ? entry.hull : entry.mesh;
            bool& failed = hull ? entry.hullFailed : entry.meshFailed;
            if (cooked == nullptr && !failed)
            {
                cooked = hull ? Cook(handle, JPH::ConvexHullShapeSettings(entry.vertices), "a convex hull")
                              : CookMesh(handle, entry);
                failed = cooked == nullptr;
                if ((entry.hull != nullptr || entry.hullFailed) && (entry.mesh != nullptr || entry.meshFailed))
                {
                    entry.vertices = {};
                    entry.triangles = {};
                }
            }
            return cooked.GetPtr();
        }

        JPH::Ref<JPH::Shape> CookMesh(CollisionMesh handle, const Entry& entry)
        {
            if (entry.triangles.empty())
            {
                logger->Error("Collision mesh " + std::to_string(handle.id) +
                              " has no triangles, so it cannot be a triangle mesh");
                return nullptr;
            }
            JPH::VertexList vertices;
            vertices.reserve(entry.vertices.size());
            for (JPH::Vec3Arg v : entry.vertices)
            {
                vertices.push_back(JPH::Float3(v.GetX(), v.GetY(), v.GetZ()));
            }
            return Cook(handle, JPH::MeshShapeSettings(std::move(vertices), entry.triangles), "a triangle mesh");
        }

        JPH::Ref<JPH::Shape> Cook(CollisionMesh handle, const JPH::ShapeSettings& settings, const char* kind)
        {
            const JPH::ShapeSettings::ShapeResult result = settings.Create();
            if (result.HasError())
            {
                logger->Error("Collision mesh " + std::to_string(handle.id) + " cannot be cooked as " + kind + ": " +
                              std::string(result.GetError()));
                return nullptr;
            }
            return result.Get();
        }

        ILogger* logger;
        // Ids start at 1 (0 is the default handle, no mesh) and only grow, so a released id is never handed out again.
        std::uint64_t nextId = 1;
        std::unordered_map<std::uint64_t, Entry> meshes;
    };

    CollisionMeshes::CollisionMeshes(ILogger& logger) : m_impl(std::make_unique<Impl>(logger))
    {
    }

    CollisionMeshes::~CollisionMeshes() = default;

    CollisionMesh CollisionMeshes::Create(std::span<const Math::Vec3> vertices, std::span<const std::uint32_t> indices)
    {
        const char* problem = nullptr;
        if (vertices.empty())
        {
            problem = "it has no vertices";
        }
        else if (indices.size() % 3 != 0)
        {
            problem = "its index count is not a multiple of three";
        }
        else if (std::any_of(indices.begin(), indices.end(), [&](std::uint32_t i) { return i >= vertices.size(); }))
        {
            problem = "an index is past the last vertex";
        }
        if (problem != nullptr)
        {
            m_impl->logger->Error(std::string("A collision mesh was not made: ") + problem);
            return CollisionMesh{};
        }

        Impl::Entry entry;
        entry.vertices.reserve(vertices.size());
        for (const Math::Vec3& v : vertices)
        {
            entry.vertices.push_back(ToJolt(v));
        }
        entry.triangles.reserve(indices.size() / 3);
        for (std::size_t i = 0; i < indices.size(); i += 3)
        {
            entry.triangles.push_back(JPH::IndexedTriangle(indices[i], indices[i + 1], indices[i + 2]));
        }
        const CollisionMesh mesh{m_impl->nextId++};
        m_impl->meshes.emplace(mesh.id, std::move(entry));
        return mesh;
    }

    bool CollisionMeshes::Release(CollisionMesh mesh)
    {
        if (m_impl->meshes.erase(mesh.id) == 0)
        {
            m_impl->logger->Warn("Collision mesh " + std::to_string(mesh.id) +
                                 " was released, but it is not alive (never made, or already released)");
            return false;
        }
        return true;
    }

    struct World::Impl
    {
        Impl(CollisionMeshes& worldMeshes, ILogger& worldLogger)
            : meshes(&worldMeshes.Implementation()), logger(&worldLogger)
        {
        }

        // The backend shape for a collider, the entity's scale baked in. Identical primitives share one shape; hulls
        // and meshes share their cooked shape, wrapped per body when scaled (they take any scale per axis).
        JPH::Ref<JPH::Shape> MakeShape(const CollisionShape& shape, const Math::Vec3& scale)
        {
            if (IsPrimitive(shape))
            {
                const Primitive primitive = Bake(shape, scale);
                if (const auto it = primitives.find(primitive); it != primitives.end())
                {
                    return it->second;
                }
                PruneUnused();
                return primitives.emplace(primitive, Make(primitive)).first->second;
            }
            const JPH::Shape* cooked = meshes->Shape(shape);
            JPH_ASSERT(cooked != nullptr);
            const JPH::Shape::ShapeResult scaled = cooked->ScaleShape(cooked->MakeScaleValid(ToJolt(scale)));
            return scaled.Get();
        }

        // Forgets cached shapes no body uses any more, each time the cache has doubled since it was last pruned.
        void PruneUnused()
        {
            if (primitives.size() < pruneAt)
            {
                return;
            }
            std::erase_if(primitives, [](const auto& entry) { return entry.second->GetRefCount() == 1; });
            pruneAt = std::max(minimumPruneAt, 2 * primitives.size());
        }

        static constexpr std::size_t minimumPruneAt = 64;

        CollisionMeshes::Impl* meshes;
        // The private shape cache keyed by value: one backend shape per distinct primitive.
        std::map<Primitive, JPH::Ref<JPH::Shape>> primitives;
        std::size_t pruneAt = minimumPruneAt;
        ILogger* logger;
        BroadPhaseLayers broadPhaseLayers;
        ObjectVsBroadPhase objectVsBroadPhase;
        ObjectPairs objectPairs;
        JPH::TempAllocatorImpl tempAllocator{tempAllocatorBytes};
        JPH::JobSystemSingleThreaded jobs{JPH::cMaxPhysicsJobs};
        JPH::PhysicsSystem system;

        // Every call comes from the thread that runs the App, never during a step, so no body locks are needed.
        JPH::BodyInterface& Bodies()
        {
            return system.GetBodyInterfaceNoLock();
        }

        JPH::AABox Bounds(BodyId id)
        {
            JPH::BodyLockRead lock(system.GetBodyLockInterfaceNoLock(), ToJolt(id));
            return lock.Succeeded() ? lock.GetBody().GetWorldSpaceBounds() : JPH::AABox();
        }

        // Sleeping bodies are not woken by a body that is removed, moved or reshaped beside them (Jolt wakes only the
        // body changed), so wake whatever is near where it was. Expanded so bodies resting on it are included.
        void WakeAround(const JPH::AABox& bounds)
        {
            if (!bounds.IsValid())
            {
                return;
            }
            JPH::AABox around = bounds;
            around.ExpandBy(JPH::Vec3::sReplicate(wakeMargin));
            Bodies().ActivateBodiesInAABox(around, {}, {});
        }

        template <typename Func>
        void Write(BodyId id, Func&& func)
        {
            JPH::BodyLockWrite lock(system.GetBodyLockInterfaceNoLock(), ToJolt(id));
            if (lock.Succeeded())
            {
                func(lock.GetBody());
            }
        }
    };

    World::World(const WorldSettings& settings, CollisionMeshes& meshes, ILogger& logger)
        : m_impl(std::make_unique<Impl>(meshes, logger))
    {
        m_impl->system.Init(maxBodies, 0, maxBodyPairs, maxContactConstraints, m_impl->broadPhaseLayers,
                            m_impl->objectVsBroadPhase, m_impl->objectPairs);
        m_impl->system.SetGravity(ToJolt(settings.gravity));
    }

    World::~World()
    {
        // Jolt leaves destroying bodies to its user.
        JPH::BodyIDVector ids;
        m_impl->system.GetBodies(ids);
        if (!ids.empty())
        {
            m_impl->Bodies().RemoveBodies(ids.data(), static_cast<int>(ids.size()));
            m_impl->Bodies().DestroyBodies(ids.data(), static_cast<int>(ids.size()));
        }
    }

    bool World::CanMakeShape(const CollisionShape& shape)
    {
        return IsPrimitive(shape) || m_impl->meshes->Shape(shape) != nullptr;
    }

    void World::CreateBodies(std::span<const BodyDesc> bodies, std::span<BodyId> ids)
    {
        std::vector<JPH::BodyID> created;
        created.reserve(bodies.size());
        for (std::size_t i = 0; i < bodies.size(); ++i)
        {
            const BodyDesc& desc = bodies[i];
            const JPH::Ref<JPH::Shape> shape = m_impl->MakeShape(desc.shape, desc.scale);
            JPH::BodyCreationSettings settings(shape, ToJolt(desc.position), ToJolt(desc.rotation),
                                               ToJolt(desc.motion.motionType), LayerOf(desc.motion.motionType));
            settings.mAllowDynamicOrKinematic = desc.canMove;
            settings.mUserData = desc.entity;
            settings.mLinearVelocity = ToJolt(desc.linearVelocity);
            settings.mAngularVelocity = ToJolt(desc.angularVelocity);
            settings.mLinearDamping = desc.motion.linearDamping;
            settings.mAngularDamping = desc.motion.angularDamping;
            settings.mGravityFactor = desc.motion.gravityScale;
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
            settings.mMassPropertiesOverride = MassOf(*shape, desc.motion.mass);

            JPH::Body* body = m_impl->Bodies().CreateBody(settings);
            if (body == nullptr)
            {
                m_impl->logger->Error("Physics world is full: a body was not made (" + std::to_string(maxBodies) +
                                      " at most)");
                ids[i] = BodyId{};
                continue;
            }
            ids[i] = BodyId{body->GetID().GetIndexAndSequenceNumber()};
            created.push_back(body->GetID());
        }
        if (created.empty())
        {
            return;
        }
        // Prepare may reorder the ids; the BodyIds above were taken before.
        const auto state = m_impl->Bodies().AddBodiesPrepare(created.data(), static_cast<int>(created.size()));
        m_impl->Bodies().AddBodiesFinalize(created.data(), static_cast<int>(created.size()), state,
                                           JPH::EActivation::Activate);
    }

    void World::DestroyBodies(std::span<const BodyId> ids)
    {
        std::vector<JPH::BodyID> bodies;
        bodies.reserve(ids.size());
        for (BodyId id : ids)
        {
            if (id.value != BodyId{}.value)
            {
                bodies.push_back(ToJolt(id));
            }
        }
        if (bodies.empty())
        {
            return;
        }
        std::vector<JPH::AABox> bounds;
        bounds.reserve(ids.size());
        for (BodyId id : ids)
        {
            bounds.push_back(m_impl->Bounds(id));
        }
        m_impl->Bodies().RemoveBodies(bodies.data(), static_cast<int>(bodies.size()));
        m_impl->Bodies().DestroyBodies(bodies.data(), static_cast<int>(bodies.size()));
        for (const JPH::AABox& box : bounds)
        {
            m_impl->WakeAround(box);
        }
    }

    void World::Teleport(BodyId id, const Math::Vec3& position, const Math::Quat& rotation)
    {
        const JPH::AABox before = m_impl->Bounds(id);
        m_impl->Bodies().SetPositionAndRotation(ToJolt(id), ToJolt(position), ToJolt(rotation),
                                                JPH::EActivation::Activate);
        m_impl->WakeAround(before);
        m_impl->WakeAround(m_impl->Bounds(id));
    }

    void World::MoveKinematic(BodyId id, const Math::Vec3& position, const Math::Quat& rotation, float dt)
    {
        m_impl->Bodies().MoveKinematic(ToJolt(id), ToJolt(position), ToJolt(rotation), dt);
    }

    void World::SetVelocity(BodyId id, const Math::Vec3& linear, const Math::Vec3& angular)
    {
        m_impl->Bodies().SetLinearAndAngularVelocity(ToJolt(id), ToJolt(linear), ToJolt(angular));
    }

    void World::SetShape(BodyId id, const CollisionShape& shape, const Math::Vec3& scale, float mass)
    {
        const JPH::AABox before = m_impl->Bounds(id);
        const JPH::Ref<JPH::Shape> made = m_impl->MakeShape(shape, scale);
        m_impl->Bodies().SetShape(ToJolt(id), made, false, JPH::EActivation::Activate);
        // The body keeps its own mass; its inertia follows the new shape.
        m_impl->Write(id, [&made, mass](JPH::Body& body) {
            if (JPH::MotionProperties* motion = body.GetMotionPropertiesUnchecked())
            {
                motion->SetMassProperties(JPH::EAllowedDOFs::All, MassOf(*made, mass));
            }
        });
        m_impl->WakeAround(before);
        m_impl->WakeAround(m_impl->Bounds(id));
    }

    void World::SetMotion(BodyId id, const Motion& motion)
    {
        m_impl->WakeAround(m_impl->Bounds(id));
        JPH::BodyInterface& bodies = m_impl->Bodies();
        bodies.SetObjectLayer(ToJolt(id), LayerOf(motion.motionType));
        bodies.SetMotionType(ToJolt(id), ToJolt(motion.motionType), JPH::EActivation::Activate);
        m_impl->Write(id, [&motion](JPH::Body& body) {
            JPH::MotionProperties* properties = body.GetMotionPropertiesUnchecked();
            if (properties == nullptr)
            {
                return;
            }
            properties->SetLinearDamping(motion.linearDamping);
            properties->SetAngularDamping(motion.angularDamping);
            properties->SetGravityFactor(motion.gravityScale);
            properties->ScaleToMass(motion.mass);
        });
    }

    std::size_t World::DistinctShapeCount() const
    {
        JPH::BodyIDVector ids;
        m_impl->system.GetBodies(ids);
        std::set<const JPH::Shape*> shapes;
        for (const JPH::BodyID id : ids)
        {
            JPH::BodyLockRead lock(m_impl->system.GetBodyLockInterfaceNoLock(), id);
            if (lock.Succeeded())
            {
                shapes.insert(lock.GetBody().GetShape());
            }
        }
        return shapes.size();
    }

    BodyState World::GetState(BodyId id) const
    {
        const JPH::BodyID body = ToJolt(id);
        JPH::BodyInterface& bodies = m_impl->Bodies();
        JPH::RVec3 position;
        JPH::Quat rotation;
        bodies.GetPositionAndRotation(body, position, rotation);
        return BodyState{FromJolt(position), FromJolt(rotation), FromJolt(bodies.GetLinearVelocity(body)),
                         FromJolt(bodies.GetAngularVelocity(body))};
    }

    void World::Step(float dt, int collisionSteps)
    {
        const JPH::EPhysicsUpdateError error =
            m_impl->system.Update(dt, collisionSteps, &m_impl->tempAllocator, &m_impl->jobs);
        if (error != JPH::EPhysicsUpdateError::None)
        {
            m_impl->logger->Warn("Physics step ran out of room for body pairs or contacts (error " +
                                 std::to_string(static_cast<unsigned>(error)) + "); some contacts were ignored");
        }
    }
} // namespace Fenrir::Backend
