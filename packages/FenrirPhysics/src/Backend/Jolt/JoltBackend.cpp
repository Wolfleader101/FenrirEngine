// The Jolt implementation of the physics backend seam (ADR-0010). The only code that includes Jolt.

#include "Backend/Backend.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <thread>
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
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollidePointResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
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

        // Broad-phase layers, one tree each: static bodies (rarely updated), moving bodies, and triggers. Static bodies
        // never collide with each other.
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
                constexpr JPH::BroadPhaseLayer moving(static_cast<JPH::BroadPhaseLayer::Type>(BroadPhase::Moving));
                return m_layers->Key(layer).broadPhase == BroadPhase::Moving || broadPhase == moving;
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
                if (first.broadPhase != BroadPhase::Moving && second.broadPhase != BroadPhase::Moving)
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

        // The entity's scale is baked into the shape's dimensions, so a scale change makes a new shape.
        JPH::Ref<JPH::Shape> MakeShape(const CollisionShape& shape, const Math::Vec3& scale)
        {
            const Math::Vec3 size = glm::abs(scale);
            if (shape.Is<SphereShape>())
            {
                return new JPH::SphereShape(shape.As<SphereShape>().radius * std::max({size.x, size.y, size.z}));
            }
            const Math::Vec3 half = shape.As<BoxShape>().halfExtents * size;
            // Jolt rounds a box's edges by a convex radius, which must fit inside the box.
            const float convexRadius = std::min(JPH::cDefaultConvexRadius, 0.5f * std::min({half.x, half.y, half.z}));
            return new JPH::BoxShape(ToJolt(half), convexRadius);
        }

        JPH::BodyID ToJolt(BodyId id)
        {
            return JPH::BodyID(id.value);
        }

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

    struct World::Impl
    {
        Impl(ILogger& worldLogger, ThreadPool* workers)
            : logger(&worldLogger), objectLayers(worldLogger), broadPhaseLayers(objectLayers),
              objectVsBroadPhase(objectLayers), objectPairs(objectLayers), jobs(MakeJobs(workers))
        {
        }

        ILogger* logger;
        ObjectLayers objectLayers;
        BroadPhaseLayers broadPhaseLayers;
        ObjectVsBroadPhase objectVsBroadPhase;
        ObjectPairs objectPairs;
        JPH::TempAllocatorImpl tempAllocator{tempAllocatorBytes};
        std::shared_ptr<JPH::JobSystem> jobs;
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

    World::World(const WorldSettings& settings, ILogger& logger)
        : m_impl(std::make_unique<Impl>(logger, settings.workers))
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

    void World::CreateBodies(std::span<const BodyDesc> bodies, std::span<BodyId> ids)
    {
        std::vector<JPH::BodyID> created;
        created.reserve(bodies.size());
        for (std::size_t i = 0; i < bodies.size(); ++i)
        {
            const BodyDesc& desc = bodies[i];
            JPH::BodyCreationSettings settings(MakeShape(desc.shape, desc.scale), ToJolt(desc.position),
                                               ToJolt(desc.rotation), ToJolt(desc.motion.motionType),
                                               m_impl->AcquireLayer(desc.motion.motionType, desc.filter));
            settings.mAllowDynamicOrKinematic = desc.canMove;
            settings.mUserData = desc.entity;
            settings.mLinearVelocity = ToJolt(desc.linearVelocity);
            settings.mAngularVelocity = ToJolt(desc.angularVelocity);
            settings.mLinearDamping = desc.motion.linearDamping;
            settings.mAngularDamping = desc.motion.angularDamping;
            settings.mGravityFactor = desc.motion.gravityScale;
            settings.mFriction = desc.material.friction;
            settings.mRestitution = desc.material.restitution;
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = desc.motion.mass;

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
        m_impl->Bodies().SetShape(ToJolt(id), MakeShape(shape, scale), true, JPH::EActivation::Activate);
        // The new shape's mass properties come from its volume and density; keep the body's own mass.
        m_impl->Write(id, [mass](JPH::Body& body) {
            if (JPH::MotionProperties* motion = body.GetMotionPropertiesUnchecked())
            {
                motion->ScaleToMass(mass);
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

    namespace
    {
        // A query's filter, at each of Jolt's three levels.
        class QueryBroadPhaseFilter final : public JPH::BroadPhaseLayerFilter
        {
          public:
            explicit QueryBroadPhaseFilter(const QueryFilter& filter) : m_filter(&filter)
            {
            }

            bool ShouldCollide(JPH::BroadPhaseLayer layer) const override
            {
                constexpr JPH::BroadPhaseLayer triggers(static_cast<JPH::BroadPhaseLayer::Type>(BroadPhase::Trigger));
                return m_filter->hitTriggers || layer != triggers;
            }

          private:
            const QueryFilter* m_filter;
        };

        class QueryLayerFilter final : public JPH::ObjectLayerFilter
        {
          public:
            QueryLayerFilter(const QueryFilter& filter, const ObjectLayers& layers)
                : m_filter(&filter), m_layers(&layers)
            {
            }

            bool ShouldCollide(JPH::ObjectLayer layer) const override
            {
                return (m_layers->Key(layer).layers & m_filter->mask) != 0;
            }

          private:
            const QueryFilter* m_filter;
            const ObjectLayers* m_layers;
        };

        class QueryBodyFilter final : public JPH::BodyFilter
        {
          public:
            explicit QueryBodyFilter(const QueryFilter& filter) : m_filter(&filter)
            {
            }

            bool ShouldCollideLocked(const JPH::Body& body) const override
            {
                if (body.IsSensor() && !m_filter->hitTriggers)
                {
                    return false;
                }
                return body.GetUserData() != m_filter->ignoreEntity;
            }

          private:
            const QueryFilter* m_filter;
        };

        // The three filters of one query, built together.
        struct QueryFilters
        {
            QueryFilters(const QueryFilter& filter, const ObjectLayers& layers)
                : broadPhase(filter), layer(filter, layers), body(filter)
            {
            }

            QueryBroadPhaseFilter broadPhase;
            QueryLayerFilter layer;
            QueryBodyFilter body;
        };

        std::uint32_t EntityOf(const JPH::Body& body)
        {
            return static_cast<std::uint32_t>(body.GetUserData());
        }

        // Hands each hit to a sink, narrowing the query to the distance the sink still wants.
        class RayCollector final : public JPH::CastRayCollector
        {
          public:
            RayCollector(const JPH::RRayCast& ray, float maxDistance, HitSink sink)
                : m_ray(&ray), m_maxDistance(maxDistance), m_sink(sink)
            {
            }

            void OnBody(const JPH::Body& body) override
            {
                m_entity = EntityOf(body);
            }

            void AddHit(const JPH::RayCastResult& result) override
            {
                const JPH::Vec3 point = m_ray->GetPointOnRay(result.mFraction);
                const JPH::Vec3 direction = m_ray->mDirection.Normalized();
                // A ray that starts inside a solid shape has no surface to take a normal from.
                const JPH::Vec3 normal = result.mFraction > 0.0f
                                             ? GetContext()->GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point)
                                             : -direction;
                const float wanted =
                    m_sink(QueryHit{m_entity, FromJolt(point), FromJolt(normal), result.mFraction * m_maxDistance});
                const float fraction = wanted / m_maxDistance;
                if (fraction < GetEarlyOutFraction())
                {
                    UpdateEarlyOutFraction(fraction);
                }
            }

          private:
            const JPH::RRayCast* m_ray;
            float m_maxDistance;
            HitSink m_sink;
            std::uint32_t m_entity = 0;
        };

        class ShapeCastCollector final : public JPH::CastShapeCollector
        {
          public:
            ShapeCastCollector(float maxDistance, HitSink sink) : m_maxDistance(maxDistance), m_sink(sink)
            {
            }

            void OnBody(const JPH::Body& body) override
            {
                m_entity = EntityOf(body);
            }

            void AddHit(const JPH::ShapeCastResult& result) override
            {
                const JPH::Vec3 normal = -result.mPenetrationAxis.NormalizedOr(JPH::Vec3::sZero());
                const float wanted = m_sink(QueryHit{m_entity, FromJolt(result.mContactPointOn1), FromJolt(normal),
                                                     result.mFraction * m_maxDistance});
                // Jolt orders shapes that start overlapping by negative penetration depth, below fraction 0: a sink
                // that wants nothing farther than distance 0 still sees them.
                const float fraction = wanted / m_maxDistance;
                if (fraction > 0.0f && fraction < GetEarlyOutFraction())
                {
                    UpdateEarlyOutFraction(fraction);
                }
            }

          private:
            float m_maxDistance;
            HitSink m_sink;
            std::uint32_t m_entity = 0;
        };

        // Reports each body once, however many of its parts are hit; stops when the sink says so.
        template <typename Base>
        class BodyCollector final : public Base
        {
          public:
            explicit BodyCollector(BodySink sink) : m_sink(sink)
            {
            }

            void OnBody(const JPH::Body& body) override
            {
                m_entity = EntityOf(body);
                m_reported = false;
            }

            void AddHit(const typename Base::ResultType&) override
            {
                if (m_reported)
                {
                    return;
                }
                m_reported = true;
                if (!m_sink(m_entity))
                {
                    this->ForceEarlyOut();
                }
            }

          private:
            BodySink m_sink;
            std::uint32_t m_entity = 0;
            bool m_reported = false;
        };

        JPH::Mat44 ToJolt(const Math::Vec3& position, const Math::Quat& rotation)
        {
            return JPH::Mat44::sRotationTranslation(ToJolt(rotation), ToJolt(position));
        }
    } // namespace

    void World::CastRay(const Math::Vec3& origin, const Math::Vec3& direction, float maxDistance,
                        const QueryFilter& filter, HitSink sink) const
    {
        if (!(maxDistance > 0.0f))
        {
            return;
        }
        const JPH::RRayCast ray(ToJolt(origin), ToJolt(direction) * maxDistance);
        RayCollector collector(ray, maxDistance, sink);
        const QueryFilters filters(filter, m_impl->objectLayers);
        m_impl->system.GetNarrowPhaseQueryNoLock().CastRay(ray, JPH::RayCastSettings(), collector, filters.broadPhase,
                                                           filters.layer, filters.body);
    }

    void World::CastShape(const CollisionShape& shape, const Math::Vec3& scale, const Math::Vec3& position,
                          const Math::Quat& rotation, const Math::Vec3& direction, float maxDistance,
                          const QueryFilter& filter, HitSink sink) const
    {
        if (!(maxDistance > 0.0f))
        {
            return;
        }
        const JPH::Ref<JPH::Shape> cast = MakeShape(shape, scale);
        const JPH::RShapeCast shapeCast = JPH::RShapeCast::sFromWorldTransform(
            cast, JPH::Vec3::sOne(), ToJolt(position, rotation), ToJolt(direction) * maxDistance);
        ShapeCastCollector collector(maxDistance, sink);
        const QueryFilters filters(filter, m_impl->objectLayers);
        m_impl->system.GetNarrowPhaseQueryNoLock().CastShape(shapeCast, JPH::ShapeCastSettings(), JPH::RVec3::sZero(),
                                                             collector, filters.broadPhase, filters.layer,
                                                             filters.body);
    }

    void World::CollideShape(const CollisionShape& shape, const Math::Vec3& scale, const Math::Vec3& position,
                             const Math::Quat& rotation, const QueryFilter& filter, BodySink sink) const
    {
        const JPH::Ref<JPH::Shape> overlap = MakeShape(shape, scale);
        const JPH::Mat44 centreOfMass = ToJolt(position, rotation).PreTranslated(overlap->GetCenterOfMass());
        BodyCollector<JPH::CollideShapeCollector> collector(sink);
        const QueryFilters filters(filter, m_impl->objectLayers);
        m_impl->system.GetNarrowPhaseQueryNoLock().CollideShape(
            overlap, JPH::Vec3::sOne(), centreOfMass, JPH::CollideShapeSettings(), JPH::RVec3::sZero(), collector,
            filters.broadPhase, filters.layer, filters.body);
    }

    void World::CollidePoint(const Math::Vec3& point, const QueryFilter& filter, BodySink sink) const
    {
        BodyCollector<JPH::CollidePointCollector> collector(sink);
        const QueryFilters filters(filter, m_impl->objectLayers);
        m_impl->system.GetNarrowPhaseQueryNoLock().CollidePoint(ToJolt(point), collector, filters.broadPhase,
                                                                filters.layer, filters.body);
    }

    bool World::IsSleeping(BodyId id) const
    {
        const JPH::Body* body = m_impl->system.GetBodyLockInterfaceNoLock().TryGetBody(ToJolt(id));
        // A static body never moves, so it neither sleeps nor wakes.
        return body != nullptr && !body->IsStatic() && !body->IsActive();
    }

    Math::AABB World::Bounds(BodyId id) const
    {
        const JPH::AABox bounds = m_impl->Bounds(id);
        return Math::AABB(FromJolt(bounds.mMin), FromJolt(bounds.mMax));
    }

    void World::Step(float dt, int collisionSteps)
    {
        const JPH::EPhysicsUpdateError error =
            m_impl->system.Update(dt, collisionSteps, &m_impl->tempAllocator, m_impl->jobs.get());
        if (error != JPH::EPhysicsUpdateError::None)
        {
            m_impl->logger->Warn("Physics step ran out of room for body pairs or contacts (error " +
                                 std::to_string(static_cast<unsigned>(error)) + "); some contacts were ignored");
        }
    }
} // namespace Fenrir::Backend
