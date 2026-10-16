// The Jolt implementation of the physics backend seam (ADR-0010). The only code that includes Jolt.

#include "Backend/Backend.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <string>
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
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
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
        explicit Impl(ILogger& worldLogger)
            : logger(&worldLogger), objectLayers(worldLogger), broadPhaseLayers(objectLayers),
              objectVsBroadPhase(objectLayers), objectPairs(objectLayers)
        {
        }

        ILogger* logger;
        ObjectLayers objectLayers;
        BroadPhaseLayers broadPhaseLayers;
        ObjectVsBroadPhase objectVsBroadPhase;
        ObjectPairs objectPairs;
        JPH::TempAllocatorImpl tempAllocator{tempAllocatorBytes};
        JPH::JobSystemSingleThreaded jobs{JPH::cMaxPhysicsJobs};
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

    World::World(const WorldSettings& settings, ILogger& logger) : m_impl(std::make_unique<Impl>(logger))
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
            JPH::BodyCreationSettings settings(MakeShape(desc.shape, desc.scale), ToJolt(desc.position),
                                               ToJolt(desc.rotation), ToJolt(motionType), layer);
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
            m_impl->system.Update(dt, collisionSteps, &m_impl->tempAllocator, &m_impl->jobs);
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
