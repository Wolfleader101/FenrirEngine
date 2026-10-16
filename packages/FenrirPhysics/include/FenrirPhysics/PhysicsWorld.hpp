#pragma once

#include <memory>

#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    class Entity;
    class Scene;

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
         * @brief Changes the velocity of `entity`'s body by `impulse` (newton seconds, world space) over its mass, at
         * the next step. Does nothing if the entity has no dynamic body then.
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
