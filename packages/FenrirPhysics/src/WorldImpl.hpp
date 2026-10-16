#pragma once

// A physics world's private state and its PostTick pipeline. Knows the ECS, not Jolt: it talks to Backend::World.

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
        Impl(EntityList& entities, const Backend::WorldSettings& settings, ILogger& logger);

        /**
         * @brief One tick: make and remove bodies for the components added and removed since the last tick, push the
         * edits game code made to components and world transforms, step by `dt` seconds, and write dynamic bodies'
         * poses and velocities back.
         */
        void Step(float dt);

      private:
        // The queries, in Queries.cpp, read the bodies and the backend.
        friend class PhysicsWorld;

        // What physics last applied to a body, or wrote back from it: the diff compares the components against it.
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
        };

        void SyncBodies();
        void PushEdits(float dt);
        void WriteBack();

        EntityList* m_entities;
        Backend::World m_backend;
        ComponentWatch m_rigidBodies;
        ComponentWatch m_colliders;

        // Entities whose bodies are rebuilt at the next tick: those with a collider when the world was made.
        std::set<std::uint32_t> m_pending;

        // Ordered by entity, so bodies are made, diffed and written back in a reproducible order (ADR-0011).
        std::map<std::uint32_t, Record> m_bodies;
    };
} // namespace Fenrir
