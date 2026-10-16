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
         * @brief Writes the bodies `shape`, placed at `pose` (scaled as a collider is), overlaps to `hits`, and returns
         * how many it wrote, stopping when `hits` is full.
         */
        std::size_t Overlap(const CollisionShape& shape, const Transform& pose, std::span<OverlapHit> hits,
                            const QueryFilter& filter = {}) const;

        /** @brief Writes the bodies containing `point` to `hits`, and returns how many it wrote, stopping when `hits`
         * is full. */
        std::size_t OverlapPoint(const Math::Vec3& point, std::span<OverlapHit> hits,
                                 const QueryFilter& filter = {}) const;

        /** @brief Whether the body of `body` (the entity with its RigidBody or Collider) is asleep: resting, and not
         * simulated until something wakes it. False for an entity with no body. */
        [[nodiscard]] bool IsSleeping(const Entity& body) const;

        /** @brief The world-space box around the body of `body`, or none for an entity with no body. */
        [[nodiscard]] std::optional<Math::AABB> Bounds(const Entity& body) const;

        /** @brief Private: the world's state, for FenrirPhysics's own code. */
        [[nodiscard]] Impl& Implementation()
        {
            return *m_impl;
        }

      private:
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Fenrir
