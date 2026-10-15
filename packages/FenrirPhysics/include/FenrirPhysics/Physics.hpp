#pragma once

#include <memory>

#include "FenrirMath/Math.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"

namespace Fenrir
{
    class App;

    /** @brief How Physics::Install sets physics up for an app. */
    struct PhysicsConfig
    {
        /** @brief The gravity each new physics world starts with, in metres per second squared. */
        Math::Vec3 gravity = Math::Vec3(0.0f, -9.81f, 0.0f);
    };

    /**
     * @brief The owner of what Physics::Install set up: the physics backend's process-wide state and the systems that
     * step each scene's physics world. Move-only.
     *
     * Destroying it first removes every scene's physics world, then tears down the backend; the systems it installed
     * then do nothing. Destroy it before the App, and keep it alive while the App runs.
     */
    class Physics
    {
      public:
        /**
         * @brief Turns physics on for `app`. Each scene gets its own physics world the first time it runs as the active
         * scene with a RigidBody or Collider in it. In `PostTick`, the active scene's world brings its bodies up to
         * date with their components and world transforms, steps once by `Time::tickRate`, and writes dynamic bodies'
         * poses and velocities back. Inactive scenes' worlds do not step.
         */
        [[nodiscard]] static Physics Install(App& app, PhysicsConfig config = {});

        Physics(Physics&& other) noexcept;
        Physics& operator=(Physics&& other) noexcept;
        Physics(const Physics&) = delete;
        Physics& operator=(const Physics&) = delete;
        ~Physics();

        /** @brief Private: what Install set up. */
        struct State;

      private:
        explicit Physics(std::shared_ptr<State> state);

        void Shutdown() noexcept;

        // Shared with the installed systems, which hold it weakly: once the owner is gone they do nothing.
        std::shared_ptr<State> m_state;
    };
} // namespace Fenrir
