#pragma once

// A physics world's bodies, at the bottom of the world: which colliders make which body, the body records the diff
// compares the components against, rebuilding and editing the backend bodies, the pushes game code queues, and writing
// dynamic bodies back. The other features read the bodies through its const members, and it reads none of theirs.
// Knows the ECS, not Jolt: it talks to Backend::World. Private to the FenrirPhysics package.

#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <vector>

#include "Backend/Backend.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/TrackedComponents.hpp"
#include "FenrirPhysics/Components.hpp"
#include "Shown.hpp"

namespace Fenrir
{
    class EntityList;
    class ILogger;
} // namespace Fenrir

namespace Fenrir::Detail
{
    class DebugCanvas;

    /** @brief Whether the Collider on `entity` is a character body's shape, and so makes no body of its own. */
    [[nodiscard]] bool IsCharacterCollider(const Entity& entity);

    /** @brief Whether the Collider on `collider` is a solid collider beneath a character with no RigidBody of its own
     * between: it is not part of the character, and makes no body. */
    [[nodiscard]] bool IsCharacterChild(const Entity& collider);

    class Bodies
    {
      public:
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

        // What physics last applied to a body, or wrote back from it: the diff compares the components against it. A
        // body none of whose colliders can be made (their collision meshes were released), or that the world was too
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

            Shown shown{};
            // A trigger: a kinematic body following its entity, whatever RigidBody the entity has.
            bool isTrigger = false;
        };

        /** @brief What a rebuild changes, for the other features to drop what they had on it before Apply. */
        struct Rebuild
        {
            // The backend bodies about to be destroyed.
            std::vector<Backend::BodyId> destroyed;
            // The entities whose RigidBody, Collider, CharacterBody or Trigger came or went since the last rebuild.
            std::set<std::uint32_t> changed;
        };

        /** @brief A way of pushing a body: one of Backend::World's AddForce, AddImpulse, AddTorque and
         * AddAngularImpulse. */
        using Pusher = void (Backend::World::*)(Backend::BodyId, const Math::Vec3&);

        /** @brief The bodies of `entities`, including the colliders already there, made in `backend`. */
        Bodies(EntityList& entities, ILogger& logger, Backend::World& backend);

        /**
         * @brief The first half of a rebuild: drains the body components' tracked sets, works out which colliders make
         * which body from the hierarchy as it is now, warns about RigidBodies with no collider and colliders beneath a
         * character, and forgets the bodies to rebuild: those whose entity or colliders changed, whose set of colliders
         * changed, or whose colliders' shapes became usable or unusable.
         */
        [[nodiscard]] Rebuild Plan();

        /** @brief The second half: destroys the bodies `rebuild` names, then makes the planned ones in one batch, in
         * entity order. */
        void Apply(const Rebuild& rebuild);

        /** @brief Pushes the edits game code made to each made body's components and world transform since the last
         * step of `dt` seconds: motion, shape, material, filter, pose (a kinematic body moved, any other teleported)
         * and velocity. */
        void PushEdits(float dt);

        /** @brief Queues a push of `entity`'s body for the next step; nothing for a handle to an entity no longer
         * alive (destroyed, its id perhaps reused by a newer entity). */
        void Queue(const Entity& entity, Pusher pusher, const Math::Vec3& amount);

        /** @brief Applies the queued pushes, in the order game code made them, to the bodies that have one. */
        void ApplyPushes();

        /** @brief After the step: the dynamic body of `id` writes its pose and velocity back to its entity. */
        void WriteBack(std::uint32_t id);

        /** @brief Draws each made body's colliders. */
        void DrawColliders(DebugCanvas& canvas) const;

        /** @brief Draws each made body's velocity, if `velocities`, and its bounds, if `bounds`. */
        void DrawMotion(DebugCanvas& canvas, bool velocities, bool bounds) const;

        // What the other features read.

        /** @brief The records, by the body's entity (its RigidBody's, or a static collider's own), in entity order. */
        [[nodiscard]] const std::map<std::uint32_t, Record>& Records() const
        {
            return m_bodies;
        }

        /** @brief The record of the body whose entity is `entity`, made or inactive; null if it has none. A compound
         * body's child collider has none. */
        [[nodiscard]] const Record* RecordOf(std::uint32_t entity) const;

        /**
         * @brief The body `entity` has or belongs to: its own (a RigidBody's, or a static collider's), or, for a
         * collider of a compound body, that body. Null for an entity with neither component, or whose body is not made.
         */
        [[nodiscard]] const Record* FindBody(std::uint32_t entity) const;

        /** @brief As FindBody for its id, but null for a handle to an entity no longer alive, whose id may since name a
         * newer entity. */
        [[nodiscard]] const Record* FindBody(const Entity& entity) const;

        /**
         * @brief The entity of the body `entity` stands for: for an entity with a RigidBody or Collider, the body it
         * has or belongs to (see BodyEntityOf); otherwise, or once destroyed, itself.
         */
        [[nodiscard]] std::uint32_t ResolveBody(std::uint32_t entity) const;

        /** @brief The body a trigger is attached to: its nearest ancestor's with a RigidBody, or the nearest ancestor
         * character, else the trigger itself. */
        [[nodiscard]] std::uint32_t AttachedBodyOf(std::uint32_t trigger) const;

        /** @brief The records, for the pose code on the world (Restore, Blend and advancing the shown poses) until
         * Poses owns how bodies are shown. */
        [[nodiscard]] std::map<std::uint32_t, Record>& PoseRecords()
        {
            return m_bodies;
        }

      private:
        /**
         * @brief The entity whose body the collider on `entity` belongs to: the nearest of it and its ancestors with a
         * RigidBody, or the collider's own entity (a static body) if none has one.
         */
        [[nodiscard]] std::uint32_t BodyEntityOf(std::uint32_t entity) const;

        /** @brief The body's colliders as they are now, in the order of `entities`. */
        [[nodiscard]] std::vector<Part> PartsOf(std::span<const std::uint32_t> entities, std::uint32_t body,
                                                const Math::Vec3& bodyScale) const;

        /** @brief The parts that go into the body's backend shape. */
        [[nodiscard]] static std::vector<Backend::ColliderPart> UsableParts(std::span<const Part> parts);

        /** @brief Warns about the parts' unusable shapes and ignored filters, those `before` did not already have. */
        void WarnAbout(std::span<const Part> parts, std::span<const Part> before);

        EntityList* m_entities;
        ILogger* m_logger;
        Backend::World* m_backend;
        // The entities with each component, as of the last Plan. Of the character bodies and the triggers only what
        // came or went is read.
        TrackedComponents m_rigidBodyEntities;
        TrackedComponents m_colliderEntities;
        TrackedComponents m_characterEntities;
        TrackedComponents m_triggerEntities;

        // RigidBodies with no collider: inactive in physics. Each is warned about once, when it becomes one.
        std::set<std::uint32_t> m_empty;
        // Solid colliders beneath a character: each is warned about once, when it becomes one.
        std::set<std::uint32_t> m_characterChildren;

        // By the body's entity (its RigidBody's, or a static collider's own), so bodies are made, diffed and written
        // back in a reproducible order (ADR-0011).
        std::map<std::uint32_t, Record> m_bodies;
        // The bodies Plan found to make, each with its colliders in entity order, by the body's entity.
        std::map<std::uint32_t, std::vector<std::uint32_t>> m_toMake;

        struct Push
        {
            std::uint32_t entity;
            Pusher pusher;
            Math::Vec3 amount;
        };

        // In the order game code made them, applied after the edits are pushed and before the step.
        std::vector<Push> m_pushes;
    };
} // namespace Fenrir::Detail
