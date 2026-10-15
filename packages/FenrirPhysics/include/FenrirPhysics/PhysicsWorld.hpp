#pragma once

#include <memory>

namespace Fenrir
{
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

        /** @brief Private: the world's state, for FenrirPhysics's own code. */
        [[nodiscard]] Impl& Implementation()
        {
            return *m_impl;
        }

      private:
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Fenrir
