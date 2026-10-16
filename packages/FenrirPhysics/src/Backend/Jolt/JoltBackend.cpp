// The Jolt implementation of the physics backend seam (ADR-0010). The only code that includes Jolt.

#include "Backend/Backend.hpp"

#include <algorithm>
#include <bit>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "FenrirLogger/ILogger.hpp"
#include "FenrirScheduler/ThreadPool.hpp"

#include <Jolt/Jolt.h>

JPH_SUPPRESS_WARNING_PUSH
JPH_SUPPRESS_WARNINGS
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/FixedSizeFreeList.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemWithBarrier.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/ContactListener.h>
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

        // Broad-phase layers, one tree each: static bodies (rarely updated), moving bodies, and triggers.
        enum class BroadPhase : std::uint8_t
        {
            Static,
            Moving,
            Trigger
        };
        constexpr JPH::uint broadPhaseCount = 3;

        BroadPhase BroadPhaseOf(MotionType type)
        {
            return type == MotionType::Static ? BroadPhase::Static : BroadPhase::Moving;
        }

        // Moving bodies pair with everything. Triggers pair with static bodies too (they are kept awake, so they find
        // sleeping and static bodies), but not with each other; static bodies never pair with each other.
        bool BroadPhasesPair(BroadPhase a, BroadPhase b)
        {
            if (a == BroadPhase::Moving || b == BroadPhase::Moving)
            {
                return true;
            }
            return a != b;
        }

        // What an object layer stands for: a broad-phase layer and a collision filter.
        struct LayerKey
        {
            BroadPhase broadPhase;
            std::uint32_t layers;
            std::uint32_t mask;

            auto operator<=>(const LayerKey&) const = default;
        };

        // Each distinct LayerKey in use gets its own object layer, so the filters below evaluate the layer and mask
        // rule on any of the 32 layers (Jolt's ready-made mask filter manages 16). A layer no body uses any more is
        // reused. Changed only between steps, from the thread that runs the App; the filters read it during a step.
        class ObjectLayers
        {
          public:
            explicit ObjectLayers(ILogger& logger) : m_logger(&logger)
            {
                // The default filter for each broad-phase layer is always there, the fallback if the layers run out.
                for (JPH::uint i = 0; i < broadPhaseCount; ++i)
                {
                    const LayerKey key{static_cast<BroadPhase>(i), CollisionFilter{}.layers, CollisionFilter{}.mask};
                    m_byKey.emplace(key, static_cast<JPH::ObjectLayer>(m_entries.size()));
                    m_entries.push_back(Entry{key, 1});
                }
            }

            /** The object layer for `key`, counted as used by one more body. */
            JPH::ObjectLayer Acquire(const LayerKey& key)
            {
                JPH::ObjectLayer layer;
                if (const auto it = m_byKey.find(key); it != m_byKey.end())
                {
                    layer = it->second;
                }
                else if (!m_free.empty())
                {
                    layer = m_free.back();
                    m_free.pop_back();
                    m_entries[layer].key = key;
                    m_byKey.emplace(key, layer);
                }
                else if (m_entries.size() < JPH::cObjectLayerInvalid)
                {
                    layer = static_cast<JPH::ObjectLayer>(m_entries.size());
                    m_entries.push_back(Entry{key, 0});
                    m_byKey.emplace(key, layer);
                }
                else
                {
                    m_logger->Error("Physics world uses too many different collision filters at once (" +
                                    std::to_string(JPH::cObjectLayerInvalid) +
                                    " at most): a body collides with every layer instead");
                    layer = static_cast<JPH::ObjectLayer>(key.broadPhase);
                }
                ++m_entries[layer].users;
                return layer;
            }

            /** One body fewer uses `layer`. */
            void Release(JPH::ObjectLayer layer)
            {
                Entry& entry = m_entries[layer];
                if (--entry.users == 0)
                {
                    m_byKey.erase(entry.key);
                    m_free.push_back(layer);
                }
            }

            [[nodiscard]] const LayerKey& Key(JPH::ObjectLayer layer) const
            {
                return m_entries[layer].key;
            }

          private:
            struct Entry
            {
                LayerKey key;
                std::uint32_t users;
            };

            ILogger* m_logger;
            std::vector<Entry> m_entries;
            std::map<LayerKey, JPH::ObjectLayer> m_byKey;
            std::vector<JPH::ObjectLayer> m_free;
        };

        class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
        {
          public:
            explicit BroadPhaseLayers(const ObjectLayers& layers) : m_layers(&layers)
            {
            }

            JPH::uint GetNumBroadPhaseLayers() const override
            {
                return broadPhaseCount;
            }

            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
            {
                return JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(m_layers->Key(layer).broadPhase));
            }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
            {
                constexpr const char* names[broadPhaseCount] = {"Static", "Moving", "Trigger"};
                return names[layer.GetValue()];
            }
#endif

          private:
            const ObjectLayers* m_layers;
        };

        class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
        {
          public:
            explicit ObjectVsBroadPhase(const ObjectLayers& layers) : m_layers(&layers)
            {
            }

            bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override
            {
                return BroadPhasesPair(m_layers->Key(layer).broadPhase, static_cast<BroadPhase>(broadPhase.GetValue()));
            }

          private:
            const ObjectLayers* m_layers;
        };

        class ObjectPairs final : public JPH::ObjectLayerPairFilter
        {
          public:
            explicit ObjectPairs(const ObjectLayers& layers) : m_layers(&layers)
            {
            }

            // The layer and mask rule, symmetric as Jolt requires.
            bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
            {
                const LayerKey& first = m_layers->Key(a);
                const LayerKey& second = m_layers->Key(b);
                if (!BroadPhasesPair(first.broadPhase, second.broadPhase))
                {
                    return false;
                }
                return (first.layers & second.mask) != 0 && (second.layers & first.mask) != 0;
            }

          private:
            const ObjectLayers* m_layers;
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

        // One touching pair of sub-shapes of two bodies.
        struct TouchKey
        {
            std::uint32_t body1;
            std::uint32_t body2;
            std::uint32_t subShape1;
            std::uint32_t subShape2;

            auto operator<=>(const TouchKey&) const = default;
        };

        // Collects the pairs Jolt finds touching during a step. Jolt calls it from its job threads, several at once,
        // in no reproducible order across pairs (callbacks for one pair are serialized), so the step sorts what it
        // collects before using it.
        class TouchListener final : public JPH::ContactListener
        {
          public:
            struct Found
            {
                TouchKey key;
                Touch touch;
            };

            void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold,
                                JPH::ContactSettings&) override
            {
                Record(body1, body2, manifold);
            }

            void OnContactPersisted(const JPH::Body& body1, const JPH::Body& body2,
                                    const JPH::ContactManifold& manifold, JPH::ContactSettings&) override
            {
                Record(body1, body2, manifold);
            }

            /** What was found since the last call, ordered by pair, each pair's findings in the order found. */
            std::vector<Found> Take()
            {
                std::vector<Found> found;
                {
                    const std::lock_guard<std::mutex> lock(m_mutex);
                    found.swap(m_found);
                }
                std::stable_sort(found.begin(), found.end(),
                                 [](const Found& a, const Found& b) { return a.key < b.key; });
                return found;
            }

          private:
            static Touch Describe(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold)
            {
                Touch touch;
                touch.body1 = BodyId{body1.GetID().GetIndexAndSequenceNumber()};
                touch.body2 = BodyId{body2.GetID().GetIndexAndSequenceNumber()};
                touch.entity1 = static_cast<std::uint32_t>(body1.GetUserData());
                touch.entity2 = static_cast<std::uint32_t>(body2.GetUserData());
                touch.isTrigger1 = body1.IsSensor();
                touch.isTrigger2 = body2.IsSensor();

                // Midway between the bodies' contact points: they differ by the penetration.
                JPH::Vec3 sum = JPH::Vec3::sZero();
                const JPH::uint count = manifold.mRelativeContactPointsOn1.size();
                for (JPH::uint i = 0; i < count; ++i)
                {
                    sum += manifold.mRelativeContactPointsOn1[i] + manifold.mRelativeContactPointsOn2[i];
                }
                const JPH::RVec3 point =
                    count > 0 ? manifold.mBaseOffset + sum / (2.0f * static_cast<float>(count)) : manifold.mBaseOffset;
                touch.point = FromJolt(JPH::Vec3(point));
                touch.normal = FromJolt(manifold.mWorldSpaceNormal);
                touch.relativeSpeed =
                    (body1.GetPointVelocity(point) - body2.GetPointVelocity(point)).Dot(manifold.mWorldSpaceNormal);
                return touch;
            }

            void Record(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold)
            {
                // Jolt does not always name a pair's bodies in the same order: one pair must have one key.
                if (body2.GetID() < body1.GetID())
                {
                    Record(body2, body1, manifold.SwapShapes());
                    return;
                }
                Found found{TouchKey{body1.GetID().GetIndexAndSequenceNumber(),
                                     body2.GetID().GetIndexAndSequenceNumber(), manifold.mSubShapeID1.GetValue(),
                                     manifold.mSubShapeID2.GetValue()},
                            Describe(body1, body2, manifold)};
                const std::lock_guard<std::mutex> lock(m_mutex);
                m_found.push_back(found);
            }

            std::mutex m_mutex;
            std::vector<Found> m_found;
        };
        // Runs a step's jobs on the App's worker threads. The stepping thread is not one of them: it waits on a
        // barrier, which runs the barrier's jobs itself while it waits (JobSystemWithBarrier). A job already run that
        // way does nothing when a worker reaches it, so the step finishes even with every worker busy elsewhere, or if
        // the stepping thread is itself a worker: there is no deadlock to wait for.
        //
        // A queued job may still be waiting for a worker after the step returns, and releasing it frees it into this
        // job system: each queued job keeps the job system alive (shared ownership) rather than the world waiting.
        class PoolJobs final : public JPH::JobSystemWithBarrier, public std::enable_shared_from_this<PoolJobs>
        {
          public:
            explicit PoolJobs(ThreadPool& pool) : JPH::JobSystemWithBarrier(JPH::cMaxPhysicsBarriers), m_pool(&pool)
            {
                m_jobs.Init(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsJobs);
            }

            // The workers, and the stepping thread working while it waits.
            int GetMaxConcurrency() const override
            {
                return static_cast<int>(m_pool->ThreadCount()) + 1;
            }

            JPH::JobHandle CreateJob(const char* name, JPH::ColorArg color, const JobFunction& function,
                                     JPH::uint32 dependencies) override
            {
                JPH::uint32 index = Jobs::cInvalidObjectIndex;
                while ((index = m_jobs.ConstructObject(name, color, this, function, dependencies)) ==
                       Jobs::cInvalidObjectIndex)
                {
                    std::this_thread::yield(); // all cMaxPhysicsJobs in use: wait for one to be freed
                }
                Job* job = &m_jobs.Get(index);
                JPH::JobHandle handle(job); // the job may finish as soon as it is queued
                if (dependencies == 0)
                {
                    QueueJob(job);
                }
                return handle;
            }

          protected:
            void QueueJob(Job* job) override
            {
                job->AddRef();
                m_pool->Enqueue([self = shared_from_this(), job] {
                    job->Execute();
                    job->Release();
                });
            }

            void QueueJobs(Job** jobs, JPH::uint count) override
            {
                for (JPH::uint i = 0; i < count; ++i)
                {
                    QueueJob(jobs[i]);
                }
            }

            void FreeJob(Job* job) override
            {
                m_jobs.DestructObject(job);
            }

          private:
            using Jobs = JPH::FixedSizeFreeList<Job>;

            ThreadPool* m_pool;
            Jobs m_jobs;
        };

        // The job system a world steps with: the App's worker threads, or none (every job runs as it is queued).
        std::shared_ptr<JPH::JobSystem> MakeJobs(ThreadPool* workers)
        {
            if (workers == nullptr)
            {
                return std::make_shared<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
            }
            return std::make_shared<PoolJobs>(*workers);
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
        Impl(CollisionMeshes& worldMeshes, ILogger& worldLogger, ThreadPool* workers)
            : meshes(&worldMeshes.Implementation()), logger(&worldLogger), objectLayers(worldLogger),
              broadPhaseLayers(objectLayers), objectVsBroadPhase(objectLayers), objectPairs(objectLayers),
              jobs(MakeJobs(workers))
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
        ObjectLayers objectLayers;
        BroadPhaseLayers broadPhaseLayers;
        ObjectVsBroadPhase objectVsBroadPhase;
        ObjectPairs objectPairs;
        JPH::TempAllocatorImpl tempAllocator{tempAllocatorBytes};
        std::shared_ptr<JPH::JobSystem> jobs;
        JPH::PhysicsSystem system;
        TouchListener listener;

        // The pairs touching after the last step, and what changed in that step.
        std::map<TouchKey, Touch> touching;
        std::vector<Touch> touches;
        std::vector<TouchChange> changes;

        // Brings `touching` up to date after a step. A pair Jolt found is touching. A pair it did not find has
        // separated, unless neither body was awake when the step began: Jolt drops the contacts of sleeping bodies
        // without their separating, and finds them again when one wakes, and a body falling asleep must not end what it
        // touches.
        void UpdateTouches(const std::set<std::uint32_t>& awakeAtStart)
        {
            changes.clear();
            std::set<TouchKey> found;
            for (TouchListener::Found& each : listener.Take())
            {
                found.insert(each.key);
                const auto it = touching.find(each.key);
                if (it == touching.end())
                {
                    touching.emplace(each.key, each.touch);
                    changes.push_back(TouchChange{TouchChange::Kind::Begin, each.touch});
                }
                else
                {
                    it->second.point = each.touch.point;
                    it->second.normal = each.touch.normal;
                }
            }
            for (auto it = touching.begin(); it != touching.end();)
            {
                const TouchKey& key = it->first;
                const bool ended =
                    !found.contains(key) &&
                    (awakeAtStart.contains(key.body1) || awakeAtStart.contains(key.body2) ||
                     !Bodies().IsAdded(JPH::BodyID(key.body1)) || !Bodies().IsAdded(JPH::BodyID(key.body2)));
                if (ended)
                {
                    changes.push_back(TouchChange{TouchChange::Kind::End, it->second});
                    it = touching.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            touches.clear();
            for (const auto& [key, touch] : touching)
            {
                touches.push_back(touch);
            }
        }

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

        JPH::ObjectLayer AcquireLayer(MotionType type, const CollisionFilter& filter)
        {
            return objectLayers.Acquire(LayerKey{BroadPhaseOf(type), filter.layers, filter.mask});
        }

        // Moves a body to the object layer for `key` (after `change` edits its key), waking it and what is near it:
        // pairs it now does or no longer collides with are found again only for awake bodies.
        template <typename Change>
        void Relayer(BodyId id, Change&& change)
        {
            const JPH::ObjectLayer old = Bodies().GetObjectLayer(ToJolt(id));
            LayerKey key = objectLayers.Key(old);
            change(key);
            if (key == objectLayers.Key(old))
            {
                return;
            }
            Bodies().SetObjectLayer(ToJolt(id), objectLayers.Acquire(key));
            objectLayers.Release(old);
            WakeAround(Bounds(id));
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
        : m_impl(std::make_unique<Impl>(meshes, logger, settings.workers))
    {
        m_impl->system.Init(maxBodies, 0, maxBodyPairs, maxContactConstraints, m_impl->broadPhaseLayers,
                            m_impl->objectVsBroadPhase, m_impl->objectPairs);
        m_impl->system.SetGravity(ToJolt(settings.gravity));
        m_impl->system.SetContactListener(&m_impl->listener);
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
            const MotionType motionType = desc.isTrigger ? MotionType::Kinematic : desc.motion.motionType;
            const JPH::ObjectLayer layer =
                desc.isTrigger
                    ? m_impl->objectLayers.Acquire(LayerKey{BroadPhase::Trigger, desc.filter.layers, desc.filter.mask})
                    : m_impl->AcquireLayer(motionType, desc.filter);
            const JPH::Ref<JPH::Shape> shape = m_impl->MakeShape(desc.shape, desc.scale);
            JPH::BodyCreationSettings settings(shape, ToJolt(desc.position), ToJolt(desc.rotation), ToJolt(motionType),
                                               layer);
            settings.mAllowDynamicOrKinematic = desc.canMove;
            if (desc.isTrigger)
            {
                // A kinematic sensor kept awake sees sleeping bodies, and with this flag static ones too.
                settings.mIsSensor = true;
                settings.mCollideKinematicVsNonDynamic = true;
                settings.mAllowSleeping = false;
            }
            settings.mUserData = desc.entity;
            settings.mLinearVelocity = ToJolt(desc.linearVelocity);
            settings.mAngularVelocity = ToJolt(desc.angularVelocity);
            settings.mLinearDamping = desc.motion.linearDamping;
            settings.mAngularDamping = desc.motion.angularDamping;
            settings.mGravityFactor = desc.motion.gravityScale;
            settings.mFriction = desc.material.friction;
            settings.mRestitution = desc.material.restitution;
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
            settings.mMassPropertiesOverride = MassOf(*shape, desc.motion.mass);

            JPH::Body* body = m_impl->Bodies().CreateBody(settings);
            if (body == nullptr)
            {
                m_impl->objectLayers.Release(settings.mObjectLayer);
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
        for (JPH::BodyID body : bodies)
        {
            m_impl->objectLayers.Release(m_impl->Bodies().GetObjectLayer(body));
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

    void World::SetMaterial(BodyId id, const PhysicsMaterial& material)
    {
        m_impl->Bodies().SetFriction(ToJolt(id), material.friction);
        m_impl->Bodies().SetRestitution(ToJolt(id), material.restitution);
    }

    void World::SetFilter(BodyId id, const CollisionFilter& filter)
    {
        m_impl->Relayer(id, [&filter](LayerKey& key) {
            key.layers = filter.layers;
            key.mask = filter.mask;
        });
    }

    void World::SetMotion(BodyId id, const Motion& motion)
    {
        m_impl->WakeAround(m_impl->Bounds(id));
        m_impl->Relayer(id, [&motion](LayerKey& key) { key.broadPhase = BroadPhaseOf(motion.motionType); });
        JPH::BodyInterface& bodies = m_impl->Bodies();
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
        JPH::BodyIDVector awake;
        m_impl->system.GetActiveBodies(JPH::EBodyType::RigidBody, awake);
        std::set<std::uint32_t> awakeAtStart;
        for (const JPH::BodyID& id : awake)
        {
            awakeAtStart.insert(id.GetIndexAndSequenceNumber());
        }

        const JPH::EPhysicsUpdateError error =
            m_impl->system.Update(dt, collisionSteps, &m_impl->tempAllocator, m_impl->jobs.get());
        if (error != JPH::EPhysicsUpdateError::None)
        {
            m_impl->logger->Warn("Physics step ran out of room for body pairs or contacts (error " +
                                 std::to_string(static_cast<unsigned>(error)) + "); some contacts were ignored");
        }
        m_impl->UpdateTouches(awakeAtStart);
    }

    std::span<const TouchChange> World::TouchChanges() const
    {
        return m_impl->changes;
    }

    std::span<const Touch> World::Touches() const
    {
        return m_impl->touches;
    }
} // namespace Fenrir::Backend
