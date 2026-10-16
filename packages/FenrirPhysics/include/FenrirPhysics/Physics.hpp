#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "FenrirMath/Math.hpp"
#include "FenrirPhysics/CollisionLayers.hpp"
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
        /** @brief The most bodies each world holds at once. A body beyond it is not made, and that is logged. */
        std::uint32_t maxBodies = 65536;
        /** @brief The most pairs of bodies whose bounds overlap that each world tracks in a step. Pairs beyond it are
         * ignored for that step, and that is logged. */
        std::uint32_t maxBodyPairs = 65536;
        /** @brief The most contacts each world resolves in a step. Contacts beyond it are ignored for that step, and
         * that is logged. */
        std::uint32_t maxContacts = 10240;
        /** @brief Names for the collision layers, layer `i` named by `collisionLayers[i]` (at most 32), each with a
         * default mask. Optional: CollisionFilter takes plain bits whether or not a layer is named. */
        std::vector<CollisionLayer> collisionLayers;
        /**
         * @brief Whether a step spreads its work over the App's worker threads, or runs on the thread that runs the App
         * alone. Results are the same either way.
         */
        bool useWorkerThreads = true;
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
         * poses and velocities back. Inactive scenes' worlds do not step. A world's capacities are those in `config`
         * when the world is made, fixed for its life.
         */
        [[nodiscard]] static Physics Install(App& app, PhysicsConfig config = {});

        Physics(Physics&& other) noexcept;
        Physics& operator=(Physics&& other) noexcept;
        Physics(const Physics&) = delete;
        Physics& operator=(const Physics&) = delete;
        ~Physics();

        /** @brief The collision layers named in the PhysicsConfig given to Install. */
        [[nodiscard]] const CollisionLayers& Layers() const;

        /** @brief Private: what Install set up. */
        struct State;

      private:
        explicit Physics(std::shared_ptr<State> state);

        void Shutdown() noexcept;

        // Shared with the installed systems, which hold it weakly: once the owner is gone they do nothing.
        std::shared_ptr<State> m_state;
    };
} // namespace Fenrir
