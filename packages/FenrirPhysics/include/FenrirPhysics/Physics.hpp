#pragma once

// PROTOTYPE (physics spike): FenrirPhysics's public interface. No backend type appears here (ADR-0010).

#include <cstdint>
#include <memory>

#include "FenrirMath/Math.hpp"
#include "FenrirPhysics/Components.hpp"

namespace Fenrir
{
    class App;
    class Scene;

    struct PhysicsConfig
    {
        Math::Vec3 gravity = Math::Vec3(0.0f, -9.81f, 0.0f);
        std::uint32_t maxBodies = 4096;
        std::uint32_t maxBodyPairs = 4096;
        std::uint32_t maxContacts = 4096;
    };

    /** One scene's simulation, kept as a scene resource. */
    class PhysicsWorld
    {
      public:
        explicit PhysicsWorld(const PhysicsConfig& config);
        ~PhysicsWorld();
        PhysicsWorld(const PhysicsWorld&) = delete;
        PhysicsWorld& operator=(const PhysicsWorld&) = delete;

        /** The scene's world, or null if physics has not run on the scene yet. */
        static PhysicsWorld* Of(Scene& scene);

        void SetGravity(const Math::Vec3& gravity);

        /** Runs the PostTick pipeline for this scene: drain, diff, step, write back. */
        void Step(Scene& scene, float dt);

        struct Impl;

      private:
        std::unique_ptr<Impl> m_impl;
    };

    /** The owner of what Install set up. Destroying it removes every scene's world, then the backend's state. */
    class Physics
    {
      public:
        [[nodiscard]] static Physics Install(App& app, PhysicsConfig config = {});

        Physics(Physics&& other) noexcept;
        Physics& operator=(Physics&& other) noexcept;
        ~Physics();

        struct State;

      private:
        explicit Physics(std::unique_ptr<State> state);
        std::unique_ptr<State> m_state;
    };
} // namespace Fenrir
