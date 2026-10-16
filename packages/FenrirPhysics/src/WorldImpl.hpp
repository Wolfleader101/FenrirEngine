#pragma once

// A physics world's private state and its PostTick pipeline. Knows the ECS, not Jolt: it talks to Backend::World.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <vector>

#include "Backend/Backend.hpp"
#include "FenrirECS/ComponentWatch.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"

namespace Fenrir
{
    class App;
    class EntityList;
    class ILogger;

    struct PhysicsWorld::Impl
    {
        /** @brief A world for the bodies in `entities`, including the colliders already there. */
        Impl(EntityList& entities, const Backend::WorldSettings& settings, Backend::CollisionMeshes& meshes,
             ILogger& logger);

        /**
         * @brief One tick: make and remove bodies for the components added and removed since the last tick, push the
         * edits game code made to components and world transforms, step by `dt` seconds, write dynamic bodies' poses
         * and velocities back, and send the step's contact and trigger events to `app`.
         */
        void Step(App& app, float dt);

        /** @brief Calls `func` for each contact of the body `body` touching now. */
        void ForEachContact(const Entity& body, const std::function<void(const Contact&)>& func) const;

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

        /** @brief A way of pushing a body: one of Backend::World's AddForce, AddImpulse, AddTorque and
         * AddAngularImpulse. */
        using Pusher = void (Backend::World::*)(Backend::BodyId, const Math::Vec3&);

        [[nodiscard]] Math::Vec3 Gravity() const
        {
            return m_backend.Gravity();
        }

        void SetGravity(const Math::Vec3& gravity)
        {
            m_backend.SetGravity(gravity);
        }

        /** @brief Queues a push of `entity`'s body for the next step. */
        void Queue(std::uint32_t entity, Pusher pusher, const Math::Vec3& amount);

      private:
        // The queries, in Queries.cpp, read the bodies and the backend.
        friend class PhysicsWorld;

        // What physics last applied to a body, or wrote back from it: the diff compares the components against it.
        // A collider whose body could not be made (its collision mesh was released, or the world was full) keeps a
        // record with no body: it is inactive, skipped by every step, until its components or its shape change.
        struct Record
        {
            [[nodiscard]] bool Active() const
            {
                return id.value != Backend::BodyId{}.value;
            }

            Backend::BodyId id;
            bool canMove;
            Backend::Motion motion;
            CollisionShape shape;
            PhysicsMaterial material;
            CollisionFilter filter;
            Transform world;
            Math::Vec3 linearVelocity;
            Math::Vec3 angularVelocity;
            bool isTrigger;

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

        // A contact reported by a ContactBegin and not yet ended, A and B ordered as in the event.
        struct ContactKey
        {
            std::uint32_t bodyA;
            std::uint32_t bodyB;
            std::uint32_t colliderA;
            std::uint32_t colliderB;

            auto operator<=>(const ContactKey&) const = default;
        };

        // An overlap reported by a TriggerEnter and not yet exited.
        struct OverlapKey
        {
            std::uint32_t trigger;
            std::uint32_t otherBody;
            std::uint32_t otherCollider;

            auto operator<=>(const OverlapKey&) const = default;
        };

        void SyncBodies();
        void PushEdits(float dt);
        void ApplyPushes();
        void WriteBack();
        void AdvancePoses();
        void SendEvents(App& app);

        // The filter a body's backend body has: a disabled trigger's collides with nothing.
        [[nodiscard]] CollisionFilter EffectiveFilterOf(Entity& entity) const;
        [[nodiscard]] bool ReportsContacts(std::uint32_t body) const;

        void CollectUnmadeShapes(std::set<std::uint32_t>& changed);

        EntityList* m_entities;
        ILogger* m_logger;
        Backend::World m_backend;
        ComponentWatch m_rigidBodies;
        ComponentWatch m_colliders;
        ComponentWatch m_triggers;

        // Entities whose bodies are rebuilt at the next tick: those with a collider when the world was made.
        std::set<std::uint32_t> m_pending;

        // Ordered by entity, so bodies are made, diffed and written back in a reproducible order (ADR-0011).
        std::map<std::uint32_t, Record> m_bodies;

        // What game code has been told is touching, so every begin and enter gets its end and exit.
        std::set<ContactKey> m_reportedContacts;
        std::set<OverlapKey> m_reportedOverlaps;

        struct Push
        {
            std::uint32_t entity;
            Pusher pusher;
            Math::Vec3 amount;
        };

        // In the order game code made them, applied after the edits are pushed and before the step.
        std::vector<Push> m_pushes;
    };
} // namespace Fenrir
