#pragma once

// A physics world's private state and its PostTick pipeline. Knows the ECS, not Jolt: it talks to Backend::World.

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>

#include "Backend/Backend.hpp"
#include "FenrirECS/ComponentWatch.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"

namespace Fenrir
{
    class EntityList;
    class ILogger;

    struct PhysicsWorld::Impl
    {
        /** @brief A world for the bodies in `entities`, including the colliders already there. */
        Impl(EntityList& entities, const Backend::WorldSettings& settings, Backend::CollisionMeshes& meshes,
             ILogger& logger);

        /**
         * @brief One tick: make and remove bodies for the components added and removed since the last tick, push the
         * edits game code made to components and world transforms, step by `dt` seconds, and write dynamic bodies'
         * poses and velocities back.
         */
        void Step(float dt);

        /**
         * @brief `BeforeTicks`: put back the true pose of each body Blend wrote, so `Tick` systems and the diff see
         * real simulation state. A body whose `Transform` game code changed since keeps the game's value, which the
         * next step takes as a teleport.
         */
        void Restore();

        /**
         * @brief `AfterTicks`: write each interpolated dynamic or kinematic body's pose `fraction` of the way from its
         * previous step pose to its current one, parents before children.
         */
        void Blend(float fraction);

        /**
         * @brief Diagnostic private to FenrirPhysics's tests: how many distinct backend shapes the world's bodies use.
         * Colliders with identical primitive shapes (after scale) share one.
         */
        [[nodiscard]] std::size_t SharedShapeCount() const
        {
            return m_backend.DistinctShapeCount();
        }

      private:
        // What physics last applied to a body, or wrote back from it: the diff compares the components against it.
        // A collider whose shape cannot be made (its collision mesh was released) keeps a record with no body: it is
        // inactive until its shape changes.
        struct Record
        {
            Backend::BodyId id;
            bool canMove;
            Backend::Motion motion;
            CollisionShape shape;
            PhysicsMaterial material;
            CollisionFilter filter;
            Transform world;
            Math::Vec3 linearVelocity;
            Math::Vec3 angularVelocity;

            // Interpolation: the world poses after the step before last and after the last step. A teleport or a new
            // body snaps: previous = current after its next step, so nothing blends across the jump.
            Transform previous;
            Transform current;
            bool snap = true;
            // Game code wrote the Transform during Update: shown as written until a step has taken it.
            bool heldByGame = false;
            // Whether Blend wrote this body's Transform, the true local Transform it replaced, and the one it wrote.
            bool blended = false;
            Transform trueLocal;
            Transform shownLocal;
        };

        void SyncBodies();
        void PushEdits(float dt);
        void WriteBack();
        void AdvancePoses();

        void CollectUnmadeShapes(std::set<std::uint32_t>& changed);

        EntityList* m_entities;
        ILogger* m_logger;
        Backend::World m_backend;
        ComponentWatch m_rigidBodies;
        ComponentWatch m_colliders;

        // Entities whose bodies are rebuilt at the next tick: those with a collider when the world was made.
        std::set<std::uint32_t> m_pending;

        // Ordered by entity, so bodies are made, diffed and written back in a reproducible order (ADR-0011).
        std::map<std::uint32_t, Record> m_bodies;
    };
} // namespace Fenrir
