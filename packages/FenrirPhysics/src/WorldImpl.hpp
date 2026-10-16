#pragma once

// A physics world's private state and its PostTick pipeline. Knows the ECS, not Jolt: it talks to Backend::World.

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <vector>

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
         * @brief Diagnostic private to FenrirPhysics's tests: how many distinct backend shapes the world's bodies use.
         * Colliders with identical primitive shapes (after scale) share one.
         */
        [[nodiscard]] std::size_t SharedShapeCount() const
        {
            return m_backend.DistinctShapeCount();
        }

      private:
        // One collider of a body, as physics last applied it: its shape, and its pose in the body (relative to the
        // body's pose, in world units; scale is its entity's world scale). A part whose shape cannot be made (its
        // collision mesh was released) is left out of the body's backend shape until its shape changes.
        struct Part
        {
            std::uint32_t entity;
            CollisionShape shape;
            Transform pose;
            bool usable;
            // A CollisionFilter on a collider that is not on the body's own entity: ignored, with a warning.
            bool filtered;
        };

        // What physics last applied to a body, or wrote back from it: the diff compares the components against it.
        // A body none of whose colliders can be made keeps a record with no backend body: it is inactive.
        struct Record
        {
            Backend::BodyId id;
            bool canMove;
            Backend::Motion motion;
            std::vector<Part> parts;
            PhysicsMaterial material;
            CollisionFilter filter;
            Transform world;
            Math::Vec3 linearVelocity;
            Math::Vec3 angularVelocity;
        };

        void SyncBodies();
        void PushEdits(float dt);
        void WriteBack();

        /**
         * @brief The entity whose body the collider on `entity` belongs to: the nearest of it and its ancestors with a
         * RigidBody, or the collider's own entity (a static body) if none has one.
         */
        [[nodiscard]] std::uint32_t BodyEntityOf(std::uint32_t entity) const;

        /** @brief The body's colliders as they are now, in the order of `entities`. */
        [[nodiscard]] std::vector<Part> PartsOf(std::span<const std::uint32_t> entities, std::uint32_t body,
                                                const Math::Vec3& bodyScale);

        /** @brief The parts that go into the body's backend shape. */
        [[nodiscard]] static std::vector<Backend::ColliderPart> UsableParts(std::span<const Part> parts);

        /** @brief Warns about the parts' unusable shapes and ignored filters, those `before` did not already have. */
        void WarnAbout(std::span<const Part> parts, std::span<const Part> before);

        EntityList* m_entities;
        ILogger* m_logger;
        Backend::World m_backend;
        ComponentWatch m_rigidBodies;
        ComponentWatch m_colliders;

        // The entities with each component now, as the watches have reported them.
        std::set<std::uint32_t> m_rigidBodyEntities;
        std::set<std::uint32_t> m_colliderEntities;

        // RigidBodies with no collider: inactive in physics. Each is warned about once, when it becomes one.
        std::set<std::uint32_t> m_empty;

        // By the body's entity (its RigidBody's, or a static collider's own), so bodies are made, diffed and written
        // back in a reproducible order (ADR-0011).
        std::map<std::uint32_t, Record> m_bodies;
    };
} // namespace Fenrir
