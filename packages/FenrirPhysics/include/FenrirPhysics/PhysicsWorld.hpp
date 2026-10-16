#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Queries.hpp"

namespace Fenrir
{
    class Scene;

    /** @brief One contact of a body, as PhysicsWorld::ForEachContact reports it. */
    struct Contact
    {
        /** @brief The body asked about, and its collider touching. */
        Entity body;
        Entity collider;
        /** @brief The body it touches, and that body's collider. */
        Entity otherBody;
        Entity otherCollider;
        /** @brief Where they touch, in world space, as of the last step. */
        Math::Vec3 point = Math::Vec3(0.0f);
        /** @brief World space, unit length, pointing from `body` to `otherBody`, as of the last step. */
        Math::Vec3 normal = Math::Vec3(0.0f);
    };

    /**
     * @brief One scene's physics world: the bodies of that scene's colliders, simulated together. A scene resource,
     * made by the systems Physics::Install adds and ended with its scene (or when the Physics owner is destroyed).
     *
     * Queries see the world as the last completed step left it: an ECS change made since (a moved or new collider, a
     * teleport) is seen from the next tick on. Queries only read, so systems running in parallel may query one world
     * at once; physics's own systems, which sync and step the world, never run beside them. A body whose entity was
     * destroyed since the last step is not found.
     */
    class PhysicsWorld
    {
      public:
        /** @brief Private: the world's state. Only FenrirPhysics can make one. */
        struct Impl;

        /** @brief Made by FenrirPhysics; game code reaches a world through Of. */
        explicit PhysicsWorld(std::unique_ptr<Impl> impl);
        ~PhysicsWorld();
        PhysicsWorld(const PhysicsWorld&) = delete;
        PhysicsWorld& operator=(const PhysicsWorld&) = delete;

        /** @brief The scene's physics world, or null if the scene has not yet run as the active scene with physics. */
        [[nodiscard]] static PhysicsWorld* Of(Scene& scene);

        /**
         * @brief Calls `func` once for each contact the rigid body `body` has now (as of the last step), whether or not
         * contacts are reported as events. Contacts are as ContactBegin and ContactEnd tell them: one resting on
         * another that has fallen asleep is still touching. Overlaps with triggers are not contacts.
         */
        void ForEachContact(const Entity& body, const std::function<void(const Contact&)>& func) const;

        /**
         * @brief The nearest body a ray from `origin` along `direction` (any length) hits within `maxDistance`. A ray
         * that starts inside a box or sphere hits it at distance 0.
         */
        [[nodiscard]] std::optional<RaycastHit> Raycast(const Math::Vec3& origin, const Math::Vec3& direction,
                                                        float maxDistance, const QueryFilter& filter = {}) const;

        /**
         * @brief Writes the hits of a ray, as for Raycast, to `hits`, nearest first, and returns how many it wrote.
         * When there are more hits than `hits` holds, the nearest ones are written.
         */
        std::size_t RaycastAll(const Math::Vec3& origin, const Math::Vec3& direction, float maxDistance,
                               std::span<RaycastHit> hits, const QueryFilter& filter = {}) const;

        /**
         * @brief The first body `shape`, placed at `pose` (scaled as a collider is), touches when swept along
         * `direction` (any length) for up to `maxDistance`. A shape that starts touching a body hits it at distance 0.
         */
        [[nodiscard]] std::optional<ShapeCastHit> ShapeCast(const CollisionShape& shape, const Transform& pose,
                                                            const Math::Vec3& direction, float maxDistance,
                                                            const QueryFilter& filter = {}) const;

        /**
         * @brief Writes the hits of a shape cast, as for ShapeCast, to `hits`, nearest first, and returns how many it
         * wrote. When there are more hits than `hits` holds, the nearest ones are written.
         */
        std::size_t ShapeCastAll(const CollisionShape& shape, const Transform& pose, const Math::Vec3& direction,
                                 float maxDistance, std::span<ShapeCastHit> hits, const QueryFilter& filter = {}) const;

        /**
         * @brief Writes the colliders `shape`, placed at `pose` (scaled as a collider is), overlaps to `hits`, each
         * with its body, and returns how many it wrote, stopping when `hits` is full.
         */
        std::size_t Overlap(const CollisionShape& shape, const Transform& pose, std::span<OverlapHit> hits,
                            const QueryFilter& filter = {}) const;

        /** @brief Writes the colliders containing `point` to `hits`, each with its body, and returns how many it wrote,
         * stopping when `hits` is full. */
        std::size_t OverlapPoint(const Math::Vec3& point, std::span<OverlapHit> hits,
                                 const QueryFilter& filter = {}) const;

        /** @brief Whether the body of `body` (the entity with its RigidBody or Collider; a child collider's entity
         * stands for the compound body it belongs to) is asleep: resting, and not simulated until something wakes it.
         * False for an entity with no body. */
        [[nodiscard]] bool IsSleeping(const Entity& body) const;

        /** @brief The world-space box around the body of `body`, or none for an entity with no body. */
        [[nodiscard]] std::optional<Math::AABB> Bounds(const Entity& body) const;

        /**
         * @brief Changes the velocity of `entity`'s body (for a child collider's entity, the compound body it belongs
         * to) by `impulse` (newton seconds, world space) over its mass, at the next step. Does nothing if the entity
         * has no dynamic body then.
         */
        void AddImpulse(const Entity& entity, const Math::Vec3& impulse);

        /**
         * @brief Pushes `entity`'s body with `force` (newtons, world space, through its centre of mass) for the next
         * step only: add it every tick to keep pushing. Does nothing if the entity has no dynamic body then.
         */
        void AddForce(const Entity& entity, const Math::Vec3& force);

        /**
         * @brief Twists `entity`'s body with `torque` (newton metres, world space) for the next step only: add it every
         * tick to keep twisting. Does nothing if the entity has no dynamic body then.
         */
        void AddTorque(const Entity& entity, const Math::Vec3& torque);

        /**
         * @brief Changes the angular velocity of `entity`'s body by `impulse` (newton metre seconds, world space) over
         * its moment of inertia, at the next step. Does nothing if the entity has no dynamic body then.
         */
        void AddAngularImpulse(const Entity& entity, const Math::Vec3& impulse);

        /** @brief The world's gravity, in metres per second squared. A new world starts with PhysicsConfig::gravity. */
        [[nodiscard]] Math::Vec3 Gravity() const;

        /** @brief Changes the world's gravity from the next step on, waking its bodies. Other worlds keep theirs. */
        void SetGravity(const Math::Vec3& gravity);

        /** @brief Private: the world's state, for FenrirPhysics's own code. */
        [[nodiscard]] Impl& Implementation()
        {
            return *m_impl;
        }

        /** @brief Private: the world's state, for FenrirPhysics's own code. */
        [[nodiscard]] const Impl& Implementation() const
        {
            return *m_impl;
        }

      private:
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Fenrir
