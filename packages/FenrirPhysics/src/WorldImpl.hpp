#pragma once

// A physics world's private state and its PostTick pipeline. Knows the ECS, not Jolt: it talks to Backend::World.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <vector>

#include "Backend/Backend.hpp"
#include "FenrirECS/ComponentWatch.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirScene/Scene.hpp"

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

        /** @brief Calls `func` for each contact of `entity`'s body touching now. */
        void ForEachContact(const Entity& entity, const std::function<void(const Contact&)>& func) const;

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

        /** @brief Queues a push of `entity`'s body for the next step; nothing for a handle to an entity no longer
         * alive (destroyed, its id perhaps reused by a newer entity). */
        void Queue(const Entity& entity, Pusher pusher, const Math::Vec3& amount);

      private:
        // The queries, in Queries.cpp, read the bodies and the backend.
        friend class PhysicsWorld;

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
        // A body none of whose colliders can be made (their collision meshes were released), or that the world was too
        // full to make, keeps a record with no backend body: it is inactive, skipped by every step, until its
        // components or its colliders' shapes change.
        struct Record
        {
            [[nodiscard]] bool Active() const
            {
                return id.value != Backend::BodyId{}.value;
            }

            Backend::BodyId id;
            bool canMove;
            Backend::Motion motion;
            std::vector<Part> parts;
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
            // A trigger: a kinematic body following its entity, whatever RigidBody the entity has.
            bool isTrigger = false;
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

        // Debug lines (DebugLines.cpp): remade after each step from the categories switched on, if any.
        void DrawDebugLines();
        void DrawColliders();
        struct ShapeEdgesKey;
        void DrawPart(const Part& part, const Transform& body, std::uint32_t colour,
                      std::map<ShapeEdgesKey, std::vector<Math::Vec3>>& used);

        // The filter a body's backend body has: a disabled trigger's collides with nothing.
        [[nodiscard]] CollisionFilter EffectiveFilterOf(Entity& entity) const;
        [[nodiscard]] bool ReportsContacts(std::uint32_t body) const;

        /**
         * @brief The entity whose body the collider on `entity` belongs to: the nearest of it and its ancestors with a
         * RigidBody, or the collider's own entity (a static body) if none has one.
         */
        [[nodiscard]] std::uint32_t BodyEntityOf(std::uint32_t entity) const;

        /**
         * @brief The entity of the body `entity` stands for: for an entity with a RigidBody or Collider, the body it
         * has or belongs to (see BodyEntityOf); otherwise, or once destroyed, itself.
         */
        [[nodiscard]] std::uint32_t ResolveBody(std::uint32_t entity) const;

        /** @brief The body a trigger is attached to: its nearest ancestor's with a RigidBody, else the trigger itself.
         */
        [[nodiscard]] std::uint32_t AttachedBodyOf(std::uint32_t trigger) const;

        /**
         * @brief The body `entity` has or belongs to: its own (a RigidBody's, or a static collider's), or, for a
         * collider of a compound body, that body. Null for an entity with neither component, or whose body is not made.
         */
        [[nodiscard]] Record* FindBody(std::uint32_t entity);

        /** @brief As FindBody for its id, but null for a handle to an entity no longer alive, whose id may since name a
         * newer entity. */
        [[nodiscard]] Record* FindBody(const Entity& entity);

        /** @brief The backend's form of a query's filter: its ignored body resolved as ResolveBody does, and a handle
         * to an entity no longer alive ignoring nothing. */
        [[nodiscard]] Backend::QueryFilter ToBackend(const QueryFilter& filter) const;

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
        ComponentWatch m_triggers;

        // The entities with each component now, as the watches have reported them.
        std::set<std::uint32_t> m_rigidBodyEntities;
        std::set<std::uint32_t> m_colliderEntities;

        // RigidBodies with no collider: inactive in physics. Each is warned about once, when it becomes one.
        std::set<std::uint32_t> m_empty;

        // By the body's entity (its RigidBody's, or a static collider's own), so bodies are made, diffed and written
        // back in a reproducible order (ADR-0011).
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

        // The debug lines switched on, and those the last step made.
        DebugDraw m_debugDraw = DebugDraw::None;
        std::vector<DebugLine> m_debugLines;

        // The edges of the hulls and meshes drawn by the last step, in their own space before scale, as pairs of ends:
        // asked of the backend once per collision mesh while it is drawn.
        struct ShapeEdgesKey
        {
            ShapeType type;
            std::uint64_t mesh;

            auto operator<=>(const ShapeEdgesKey&) const = default;
        };
        std::map<ShapeEdgesKey, std::vector<Math::Vec3>> m_shapeEdges;
    };

    /**
     * @brief FenrirPhysics's own way into a PhysicsWorld, for its systems: the only maker of PhysicsWorld::Key, and
     * the only reader of a world's state outside PhysicsWorld's members.
     */
    struct PhysicsWorldAccess
    {
        /** @brief Makes `scene`'s physics world, holding `impl`, as a scene resource. */
        static PhysicsWorld& Make(Scene& scene, std::unique_ptr<PhysicsWorld::Impl> impl)
        {
            return scene.AddResource<PhysicsWorld>(PhysicsWorld::Key{}, std::move(impl));
        }

        /** @brief The state of `world`. */
        [[nodiscard]] static PhysicsWorld::Impl& Of(PhysicsWorld& world)
        {
            return *world.m_impl;
        }
    };
} // namespace Fenrir
