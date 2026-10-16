#pragma once

// The physics backend seam (ADR-0010): a concrete class with no virtual calls and no backend type in sight. Its only
// implementation is in src/Backend/Jolt/, the only code that includes Jolt. Private and uninstalled.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "FenrirMath/Math.hpp"
#include "FenrirPhysics/Components.hpp"

namespace Fenrir
{
    class ILogger;
    class ThreadPool;
} // namespace Fenrir

namespace Fenrir::Backend
{
    /** @brief A body in one World. Valid until the body is destroyed. */
    struct BodyId
    {
        std::uint32_t value = ~0u;
    };

    /** @brief How a body moves and responds: the parts of RigidBody the backend needs. */
    struct Motion
    {
        MotionType motionType = MotionType::Static;
        float mass = 1.0f;
        float linearDamping = 0.0f;
        float angularDamping = 0.0f;
        float gravityScale = 1.0f;
    };

    /**
     * @brief One collider of a body: its shape, its entity's world scale (baked into the shape), and where it sits in
     * the body (relative to the body's pose, in world units).
     */
    struct ColliderPart
    {
        CollisionShape shape;
        Math::Vec3 scale = Math::Vec3(1.0f);
        Math::Vec3 position = Math::Vec3(0.0f);
        Math::Quat rotation = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
        /** @brief The collider's entity, kept with its part of the body's shape. */
        std::uint32_t entity = 0;
    };

    /** @brief Everything needed to make a body. */
    struct BodyDesc
    {
        Motion motion;
        /** @brief False for a collider with no RigidBody: it can never become kinematic or dynamic. */
        bool canMove = false;
        /**
         * @brief At least one. A single part on the body's own entity, at the body's pose, is the body's shape itself;
         * otherwise the parts are merged into one compound shape, each keeping its collider's entity.
         */
        std::vector<ColliderPart> parts;
        PhysicsMaterial material;
        CollisionFilter filter;
        Math::Vec3 position = Math::Vec3(0.0f);
        Math::Quat rotation = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
        Math::Vec3 linearVelocity = Math::Vec3(0.0f);
        Math::Vec3 angularVelocity = Math::Vec3(0.0f);
        /** @brief The body's entity, kept with the body. */
        std::uint32_t entity = 0;
        /** @brief A trigger: a kinematic body, kept awake, that pushes nothing and finds static bodies too. */
        bool isTrigger = false;
    };

    /**
     * @brief Two bodies touching, as the backend sees it: a contact, or an overlap if one is a trigger. A pair stops
     * being found when both its bodies fall asleep (or one sleeps and the other is static); it keeps touching.
     */
    struct Touch
    {
        /** @brief The two bodies, in no meaningful order. */
        BodyId body1;
        BodyId body2;
        /** @brief Their entities, as given in their BodyDesc; kept after a body is destroyed. */
        std::uint32_t entity1 = 0;
        std::uint32_t entity2 = 0;
        /** @brief The entities of the colliders touching, one of each body (a compound body's part keeps its own). */
        std::uint32_t collider1 = 0;
        std::uint32_t collider2 = 0;
        bool isTrigger1 = false;
        bool isTrigger2 = false;
        /** @brief Where they touch, in world space, as last found. */
        Math::Vec3 point = Math::Vec3(0.0f);
        /** @brief Unit length, from body 1 to body 2, as last found. */
        Math::Vec3 normal = Math::Vec3(0.0f);
        /** @brief How fast they were closing along `normal` when they began touching. */
        float relativeSpeed = 0.0f;
    };

    /** @brief A pair that began or stopped touching over one step. */
    struct TouchChange
    {
        enum class Kind : std::uint8_t
        {
            Begin,
            End
        };
        Kind kind;
        Touch touch;
    };

    /** @brief A body's pose and velocities after a step. */
    struct BodyState
    {
        Math::Vec3 position;
        Math::Quat rotation;
        Math::Vec3 linearVelocity;
        Math::Vec3 angularVelocity;
    };

    /** @brief A non-owning reference to a callable, valid while the callable is: a callback with no virtual call and
     * no allocation. */
    template <typename Signature>
    class FunctionRef;

    template <typename Result, typename... Args>
    class FunctionRef<Result(Args...)>
    {
      public:
        template <typename Callable>
        FunctionRef(Callable& callable) // NOLINT(google-explicit-constructor): passed straight to a call
            : m_callable(&callable), m_call([](void* target, Args... args) -> Result {
                  return (*static_cast<Callable*>(target))(std::forward<Args>(args)...);
              })
        {
        }

        Result operator()(Args... args) const
        {
            return m_call(m_callable, std::forward<Args>(args)...);
        }

      private:
        void* m_callable;
        Result (*m_call)(void*, Args...);
    };

    /** @brief Which bodies a query finds: those in a layer of `mask`, other than `ignoreEntity`'s, and triggers only
     * if `hitTriggers`. */
    struct QueryFilter
    {
        std::uint32_t mask = ~0u;
        /** @brief The entity whose body is not found; the default matches no entity. */
        std::uint32_t ignoreEntity = ~0u;
        bool hitTriggers = false;
    };

    /** @brief Where a ray or a cast shape hit a body. */
    struct QueryHit
    {
        /** @brief The body's entity. */
        std::uint32_t entity;
        /** @brief The entity of the body's collider that was hit. */
        std::uint32_t collider;
        Math::Vec3 point;
        Math::Vec3 normal;
        float distance;
    };

    /** @brief Given each hit nearer than the distance it last returned (at first the query's maximum), in no
     * particular order; returns the farthest distance still wanted. */
    using HitSink = FunctionRef<float(const QueryHit&)>;

    /** @brief Given the entities of each body found and of its collider, once per collider; returns false to stop the
     * query. */
    using BodySink = FunctionRef<bool(std::uint32_t, std::uint32_t)>;

    /** @brief A world's settings when it is made. Gravity can change later; the capacities cannot. */
    struct WorldSettings
    {
        Math::Vec3 gravity;
        std::uint32_t maxBodies;
        std::uint32_t maxBodyPairs;
        std::uint32_t maxContacts;
        /**
         * @brief The threads a step spreads its work over, besides the stepping thread, which also works while it
         * waits. Null: a step runs on the stepping thread alone. Must outlive the world.
         */
        ThreadPool* workers = nullptr;
    };

    /**
     * @brief The backend's process-wide state (allocator, type registration, log routing into `logger`), counted: the
     * first Context sets it up, the last one tears it down. Every World must end before the last Context does.
     */
    class Context
    {
      public:
        explicit Context(ILogger& logger);
        ~Context();
        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;

      private:
        ILogger* m_logger;
    };

    /**
     * @brief The collision meshes made by CreateCollisionMesh and not yet released: their source data and their shapes,
     * each cooked the first time it is used. Shared by every World; must outlive them all.
     */
    class CollisionMeshes
    {
      public:
        explicit CollisionMeshes(ILogger& logger);
        ~CollisionMeshes();
        CollisionMeshes(const CollisionMeshes&) = delete;
        CollisionMeshes& operator=(const CollisionMeshes&) = delete;

        /** @brief Validates and keeps the data; the default handle (and an error) if it is invalid. Ids never repeat.
         */
        [[nodiscard]] CollisionMesh Create(std::span<const Math::Vec3> vertices,
                                           std::span<const std::uint32_t> indices);

        /** @brief Forgets a mesh; bodies already using its shapes keep them until they are reshaped or destroyed. */
        bool Release(CollisionMesh mesh);

        /** @brief Private to the backend. */
        struct Impl;
        [[nodiscard]] Impl& Implementation()
        {
            return *m_impl;
        }

      private:
        std::unique_ptr<Impl> m_impl;
    };

    /**
     * @brief One simulation. Every call but a query is made from the thread that runs the App; only a step's own work
     * runs on the worker threads. Queries may come from any threads at once, never during another call.
     */
    class World
    {
      public:
        World(const WorldSettings& settings, CollisionMeshes& meshes, ILogger& logger);
        ~World();
        World(const World&) = delete;
        World& operator=(const World&) = delete;

        /**
         * @brief Whether a body can have `shape`: false for a hull or mesh whose collision mesh was released or cannot
         * be cooked as that kind (cooking it the first time, which logs an error if it fails). Says nothing otherwise.
         */
        [[nodiscard]] bool CanMakeShape(const CollisionShape& shape);

        /**
         * @brief Makes and adds the bodies in one batch, writing their ids to `ids` (the same length as `bodies`).
         * Every shape must pass CanMakeShape. A body that is not made gets the default BodyId.
         */
        void CreateBodies(std::span<const BodyDesc> bodies, std::span<BodyId> ids);

        /** @brief Removes and destroys the bodies in one batch. */
        void DestroyBodies(std::span<const BodyId> ids);

        /** @brief Moves a body to a pose at once, waking it. */
        void Teleport(BodyId id, const Math::Vec3& position, const Math::Quat& rotation);

        /** @brief Gives a kinematic body the velocity that brings it to the pose in `dt` seconds. */
        void MoveKinematic(BodyId id, const Math::Vec3& position, const Math::Quat& rotation, float dt);

        /** @brief Sets a body's velocities, waking it. */
        void SetVelocity(BodyId id, const Math::Vec3& linear, const Math::Vec3& angular);

        /**
         * @brief Replaces a body's shape with one made from `parts` (as BodyDesc::parts, for the body of `entity`),
         * keeping its `mass`; its inertia follows the new shape. Every part's shape must pass CanMakeShape.
         */
        void SetShape(BodyId id, std::span<const ColliderPart> parts, std::uint32_t entity, float mass);

        /** @brief Changes a body's surface. */
        void SetMaterial(BodyId id, const PhysicsMaterial& material);

        /** @brief Changes which layers a body is in and collides with, waking it and the bodies near it. */
        void SetFilter(BodyId id, const CollisionFilter& filter);

        /** @brief Changes how a body moves. The body must have been made with `canMove`. */
        void SetMotion(BodyId id, const Motion& motion);

        /** @brief Changes a dynamic body's velocity by `impulse` over its mass, waking it. Other bodies ignore it. */
        void AddImpulse(BodyId id, const Math::Vec3& impulse);

        /** @brief Pushes a dynamic body with `force` during the next step only, waking it. Other bodies ignore it. */
        void AddForce(BodyId id, const Math::Vec3& force);

        /** @brief Twists a dynamic body with `torque` during the next step only, waking it. Other bodies ignore it. */
        void AddTorque(BodyId id, const Math::Vec3& torque);

        /** @brief Changes a dynamic body's angular velocity by `impulse` over its inertia, waking it. Other bodies
         * ignore it. */
        void AddAngularImpulse(BodyId id, const Math::Vec3& impulse);

        [[nodiscard]] Math::Vec3 Gravity() const;

        /** @brief Changes the world's gravity, waking every body. */
        void SetGravity(const Math::Vec3& gravity);

        [[nodiscard]] BodyState GetState(BodyId id) const;

        /** @brief Diagnostic for tests: how many distinct backend shapes the world's bodies use. */
        [[nodiscard]] std::size_t DistinctShapeCount() const;

        // Queries only read: they may be made from several threads at once, never during any other call.

        /** @brief Casts a ray from `origin` along the unit `direction` for `maxDistance`. A ray that starts inside a
         * solid shape hits it at distance 0, its normal against `direction`. */
        void CastRay(const Math::Vec3& origin, const Math::Vec3& direction, float maxDistance,
                     const QueryFilter& filter, HitSink sink) const;

        /** @brief Sweeps `shape` (`scale` baked in, as for a body) from a pose along the unit `direction` for
         * `maxDistance`. Each hit's point is on the cast shape. */
        void CastShape(const CollisionShape& shape, const Math::Vec3& scale, const Math::Vec3& position,
                       const Math::Quat& rotation, const Math::Vec3& direction, float maxDistance,
                       const QueryFilter& filter, HitSink sink) const;

        /** @brief Finds the bodies `shape` (`scale` baked in, as for a body) at a pose overlaps. */
        void CollideShape(const CollisionShape& shape, const Math::Vec3& scale, const Math::Vec3& position,
                          const Math::Quat& rotation, const QueryFilter& filter, BodySink sink) const;

        /** @brief Finds the bodies containing `point`. */
        void CollidePoint(const Math::Vec3& point, const QueryFilter& filter, BodySink sink) const;

        [[nodiscard]] bool IsSleeping(BodyId id) const;

        /** @brief The body's world-space bounding box. */
        [[nodiscard]] Math::AABB Bounds(BodyId id) const;

        /**
         * @brief Advances the simulation by `dt` seconds in `collisionSteps` collision steps, and finds which pairs of
         * bodies began and stopped touching. A body destroyed since the last step ends its touches here.
         */
        void Step(float dt, int collisionSteps);

        /** @brief The pairs that began or stopped touching in the last Step, in no particular order. Valid until the
         * next Step. */
        [[nodiscard]] std::span<const TouchChange> TouchChanges() const;

        /** @brief Every pair touching after the last Step, in no particular order. Valid until the next Step. */
        [[nodiscard]] std::span<const Touch> Touches() const;

      private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Fenrir::Backend
