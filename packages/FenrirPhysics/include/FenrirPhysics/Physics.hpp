#pragma once

#include <cstdint>
#include <memory>
#include <span>
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
        /** @brief Names for the collision layers, layer `i` named by `collisionLayers[i]` (at most 32), each with a
         * default mask. Optional: CollisionFilter takes plain bits whether or not a layer is named. */
        std::vector<CollisionLayer> collisionLayers;
        /**
         * @brief Whether a step spreads its work over the App's worker threads, or runs on the thread that runs the App
         * alone. Results are the same either way.
         */
        bool useWorkerThreads = true;
        /** @brief Whether bodies are interpolated between ticks (RigidBody::interpolate). Off: every body is shown at
         * its step poses only. */
        bool interpolate = true;
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
         * poses and velocities back. Inactive scenes' worlds do not step. In `AfterTicks` it writes each interpolated
         * body's blended pose into its `Transform`, and in `BeforeTicks` restores the true pose; a `Transform` game
         * code changed in between is kept, as a teleport.
         */
        [[nodiscard]] static Physics Install(App& app, PhysicsConfig config = {});

        /**
         * @brief Cooks a collision mesh from `vertices` and, for MeshShape, the triangles `indices` lists (three per
         * triangle, counter-clockwise seen from the front). Copies what it needs, so the spans need not outlive the
         * call. Loading the data from a model or file is the caller's job.
         *
         * Each kind of shape is cooked once, the first time a collider of that kind uses the mesh, and shared by every
         * collider that refers to it in every scene. Logs an error and returns the default handle (no mesh) when there
         * are no vertices, the indices are not whole triangles, or an index is out of range. Indices may be empty for
         * a mesh used only as a hull.
         */
        [[nodiscard]] CollisionMesh CreateCollisionMesh(std::span<const Math::Vec3> vertices,
                                                        std::span<const std::uint32_t> indices);

        /**
         * @brief Releases a collision mesh made by CreateCollisionMesh. Its memory is freed once no body uses it. A
         * collider still referring to it has its body removed from the simulation at the next tick, with a warning: it
         * neither moves nor collides until it is given another shape. Releasing a mesh that is not alive warns and
         * returns false.
         */
        bool ReleaseCollisionMesh(CollisionMesh mesh);

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
