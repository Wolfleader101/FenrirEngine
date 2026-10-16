#pragma once

// The physics backend seam (ADR-0010): a concrete class with no virtual calls and no backend type in sight. Its only
// implementation is in src/Backend/Jolt/, the only code that includes Jolt. Private and uninstalled.

#include <cstdint>
#include <memory>
#include <span>
#include <utility>

#include "FenrirMath/Math.hpp"
#include "FenrirPhysics/Components.hpp"

namespace Fenrir
{
    class ILogger;
}

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

    /** @brief Everything needed to make a body. */
    struct BodyDesc
    {
        Motion motion;
        /** @brief False for a collider with no RigidBody: it can never become kinematic or dynamic. */
        bool canMove = false;
        CollisionShape shape;
        PhysicsMaterial material;
        CollisionFilter filter;
        Math::Vec3 scale = Math::Vec3(1.0f);
        Math::Vec3 position = Math::Vec3(0.0f);
        Math::Quat rotation = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
        Math::Vec3 linearVelocity = Math::Vec3(0.0f);
        Math::Vec3 angularVelocity = Math::Vec3(0.0f);
        /** @brief The body's entity, kept with the body. */
        std::uint32_t entity = 0;
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
        Math::Vec3 point;
        Math::Vec3 normal;
        float distance;
    };

    /** @brief Given each hit nearer than the distance it last returned (at first the query's maximum), in no
     * particular order; returns the farthest distance still wanted. */
    using HitSink = FunctionRef<float(const QueryHit&)>;

    /** @brief Given the entity of each body found, once per body; returns false to stop the query. */
    using BodySink = FunctionRef<bool(std::uint32_t)>;

    /** @brief A world's fixed settings. */
    struct WorldSettings
    {
        Math::Vec3 gravity;
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

    /** @brief One simulation. Single-threaded: every call is made from the thread that runs the App. */
    class World
    {
      public:
        World(const WorldSettings& settings, ILogger& logger);
        ~World();
        World(const World&) = delete;
        World& operator=(const World&) = delete;

        /** @brief Makes and adds the bodies in one batch, writing their ids to `ids` (the same length as `bodies`). */
        void CreateBodies(std::span<const BodyDesc> bodies, std::span<BodyId> ids);

        /** @brief Removes and destroys the bodies in one batch. */
        void DestroyBodies(std::span<const BodyId> ids);

        /** @brief Moves a body to a pose at once, waking it. */
        void Teleport(BodyId id, const Math::Vec3& position, const Math::Quat& rotation);

        /** @brief Gives a kinematic body the velocity that brings it to the pose in `dt` seconds. */
        void MoveKinematic(BodyId id, const Math::Vec3& position, const Math::Quat& rotation, float dt);

        /** @brief Sets a body's velocities, waking it. */
        void SetVelocity(BodyId id, const Math::Vec3& linear, const Math::Vec3& angular);

        /** @brief Replaces a body's shape, `scale` baked in, keeping its `mass`. */
        void SetShape(BodyId id, const CollisionShape& shape, const Math::Vec3& scale, float mass);

        /** @brief Changes a body's surface. */
        void SetMaterial(BodyId id, const PhysicsMaterial& material);

        /** @brief Changes which layers a body is in and collides with, waking it and the bodies near it. */
        void SetFilter(BodyId id, const CollisionFilter& filter);

        /** @brief Changes how a body moves. The body must have been made with `canMove`. */
        void SetMotion(BodyId id, const Motion& motion);

        [[nodiscard]] BodyState GetState(BodyId id) const;

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

        /** @brief Advances the simulation by `dt` seconds in `collisionSteps` collision steps. */
        void Step(float dt, int collisionSteps);

      private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Fenrir::Backend
