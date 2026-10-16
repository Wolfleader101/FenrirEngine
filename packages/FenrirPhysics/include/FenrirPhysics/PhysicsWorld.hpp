#pragma once

#include <functional>
#include <memory>

#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Math.hpp"

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

        /** @brief Private: the world's state, for FenrirPhysics's own code. */
        [[nodiscard]] Impl& Implementation()
        {
            return *m_impl;
        }

      private:
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Fenrir
