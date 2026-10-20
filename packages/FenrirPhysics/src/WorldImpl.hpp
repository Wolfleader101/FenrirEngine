#pragma once

// A physics world's private state and its PostTick pipeline. Knows the ECS, not Jolt: it talks to Backend::World.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Backend/Backend.hpp"
#include "FenrirECS/ComponentWatch.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirPhysics/Characters.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Events.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirPhysics/Vehicles.hpp"
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
         * @brief One tick: make and remove bodies and joints for the components added and removed since the last tick
         * (a joint's constraint and a vehicle before their bodies go, and after they come), push the edits game code
         * made to components and world transforms, bodies before joints and joints before vehicles, step by `dt`
         * seconds, write dynamic bodies' poses and velocities back and then vehicles' states and wheel poses, break
         * overloaded joints, and send the step's contact, trigger and JointBroken events to `app`.
         */
        void Step(App& app, float dt);

        /** @brief Calls `func` for each contact of `entity`'s body touching now. */
        void ForEachContact(const Entity& entity, const std::function<void(const Contact&)>& func) const;

        /**
         * @brief `BeforeTicks`: put back the true pose of each body and character Blend wrote, so `Tick` systems and
         * the diff see real simulation state. The parts of a `Transform` game code changed since keep the game's
         * values, and the rest go back to the true pose. The next step takes a changed body as a teleport, and a
         * character only if its position changed.
         */
        void Restore();

        /**
         * @brief `AfterTicks`: write each interpolated dynamic or kinematic body's and character's pose `fraction` of
         * the way from its previous step pose to its current one, parents before children. One that is not
         * interpolated, beneath one that is, is written at its step pose, so its parent's blend does not carry it.
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

        // How a body or character is shown between ticks: its world poses after the step before last and after the
        // last step. A teleport or a new one snaps: previous = current after its next step, so nothing blends across
        // the jump.
        struct Shown
        {
            /** @brief After a step that left it at `world`. */
            void Advance(const Transform& world);
            /** @brief `BeforeTicks`, for one Blend wrote to `local`: put back the true pose, but for the parts game
             * code changed since, which a step takes as a teleport if it moved it, or if `turnIsTeleport`. */
            void Restore(Transform& local, bool turnIsTeleport);

            Transform previous;
            Transform current;
            bool snap = true;
            // Game code wrote the Transform during Update: shown as written until a step has taken it.
            bool heldByGame = false;
            // Whether Blend wrote its Transform, the true local Transform it replaced, and the one it wrote.
            bool blended = false;
            Transform trueLocal;
            Transform shownLocal;
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

            Shown shown{};
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

        /** @brief A contact's key, A and B in entity order so the order does not depend on the backend's body ids, and
         * the touch turned to match: its normal from A to B. */
        [[nodiscard]] static std::pair<ContactKey, Backend::Touch> Oriented(const Backend::Touch& touch);

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

        // Joints (Joints.cpp). Their constraints always go before their bodies and come after them.

        /** @brief Drains the Joint watch, with the body watches: a joint removed (or removed and added again) since the
         * last tick loses its constraint, and one added and removed again comes to nothing. */
        void DrainJoints();
        /** @brief Removes the constraints of the joints naming any of `bodies`, which are about to be destroyed. */
        void RemoveJointsOf(std::span<const Backend::BodyId> bodies);
        /** @brief After the bodies' edits: makes, recreates or updates each joint's constraint, in joint entity order,
         * with its settings corrected into range for a step of `dt` seconds. */
        void SyncJoints(float dt);
        /** @brief The body `body` names, if it is a made body entity of this world; null otherwise. */
        [[nodiscard]] const Record* JointBodyOf(const Entity& body) const;
        /** @brief Why `joint` names no pair of made body entities of this world, for its warning on going inactive. */
        [[nodiscard]] std::string WhyInactive(const Joint& joint) const;
        struct JointRecord;
        /** @brief Makes the constraint `desc` describes for the joint on `entity`, indexed under its bodies. */
        void MakeConstraint(std::uint32_t entity, JointRecord& record, const Backend::JointDesc& desc);
        /** @brief Removes the joint's constraint, if it has one, and its place in the index. */
        void DropConstraint(std::uint32_t entity, JointRecord& record);
        /** @brief After the step: each joint's load, kept from the last step either of its bodies was awake for, and 0
         * while it is disabled (JointState.cpp). */
        void MeasureJoints();
        /** @brief After MeasureJoints, in joint entity order: disables each enabled joint measured this step whose load
         * is over a finite threshold, in the backend, its Joint and its record, and queues a JointBroken for SendEvents
         * (JointBreaking.cpp). */
        void BreakJoints();
        /** @brief Whether `body` names a made body that moves and is awake. */
        [[nodiscard]] bool IsAwake(const Entity& body) const;
        /** @brief PhysicsWorld::GetJointState: from each record's load and its bodies' poses after the last step. */
        [[nodiscard]] JointState StateOf(const Entity& joint) const;
        void ApplyPushes();

        // Character bodies (Characters.cpp): each a backend character, made after the bodies' batch and destroyed
        // before it.

        /** @brief Whether the Collider on `entity` is a character body's shape, and so makes no body of its own. */
        [[nodiscard]] bool IsCharacterCollider(std::uint32_t entity) const;
        /** @brief Whether the Collider on `collider` is a solid collider beneath a character with no RigidBody of its
         * own between: it is not part of the character, and makes no body. */
        [[nodiscard]] bool IsCharacterChild(std::uint32_t collider) const;
        /** @brief Destroys the backend characters of the entities in `changed`, whose components came or went since
         * the last tick (they are made again from what the entity has now), and of those disabled or gone inactive. */
        void DropCharacters(const std::set<std::uint32_t>& changed);
        /** @brief Warns once about each CharacterBody that has gone inactive, then makes, in one batch in entity order,
         * the backend character of each enabled one that can have one and has none. */
        void MakeCharacters();
        /** @brief Step 2, after the bodies' edits, in entity order: its entity's PhysicsMaterial and CollisionFilter
         * given to its body if they changed; a character whose world position differs from physics's copy is
         * teleported there (a moved parent included), and one whose world rotation alone differs is turned. Neither
         * sweeps. */
        void PushCharacterEdits();
        /** @brief Step 3b, before the step: applies each character's shape edit if it fits, then moves it by its
         * velocity over `dt` seconds, one at a time in entity order, keeping each move's result for WriteBack. */
        void MoveCharacters(float dt);
        struct CharacterRecord;
        /** @brief Step 3b, for one character before its move: gives it its Collider's shape and world scale if they
         * changed and fit, keeping the feet in place on the ground and the head in the air; otherwise marks it
         * blocked, keeping the shape it has. */
        void ApplyShape(const Entity& entity, CharacterRecord& record);
        /** @brief Step 5, for one character: its position, trimmed velocity and state, from its last move, whether its
         * Collider's shape or scale is still waiting to fit, and the world transform the diff compares against. */
        void WriteBackCharacter(Entity& entity, CharacterRecord& record);
        /** @brief Step 5, for each CharacterBody with no backend character (disabled or inactive): its state reset to
         * the defaults, and nothing else written. */
        void ResetIdleCharacters();

        // Vehicles (Vehicles.cpp): each a backend vehicle on its chassis's body, removed before its body goes, made
        // after the joints in step 2, and written back after the bodies in step 5.

        /** @brief Drains the Vehicle and VehicleWheel watches, with the body watches: a vehicle whose Vehicle or one of
         * whose wheels came or went since the last tick loses its backend vehicle, made again in step 2. */
        void DrainVehicles();
        /** @brief Removes the backend vehicles on any of `bodies`, which are about to be destroyed. */
        void RemoveVehiclesOf(std::span<const Backend::BodyId> bodies);
        struct VehicleRecord;
        /** @brief Removes the vehicle's backend vehicle, if it has one, and forgets what was applied to it. */
        void DropVehicle(VehicleRecord& record);
        /** @brief The made dynamic body of the chassis on `chassis`, if it can be a vehicle's; null otherwise. */
        [[nodiscard]] const Record* ChassisBodyOf(std::uint32_t chassis) const;
        /** @brief Each vehicle's wheel entities that can be its wheels, in entity order, by chassis entity. */
        [[nodiscard]] std::map<std::uint32_t, std::vector<std::uint32_t>> WheelsByVehicle() const;
        /** @brief Step 2, after the joints, in chassis entity order: makes the backend vehicle of each chassis with a
         * dynamic body and wheels, remade when its body or wheel set changed, and gives it its driver input, waking the
         * chassis on a change. */
        void SyncVehicles();
        /** @brief Writes each of the vehicle's wheel entities' world position and rotation from `read`, against the
         * chassis pose `chassis`, parents before children. */
        void PoseWheels(const VehicleRecord& record, const Backend::VehicleRead& read, const Transform& chassis);
        /** @brief Step 5, after the bodies: each vehicle's state, its wheels' states and their poses; the states of
         * those with no backend vehicle reset. */
        void WriteBackVehicles();

        void WriteBack();
        void AdvancePoses();
        void SendEvents(App& app);
        /** @brief Step 6, before the events are sorted: brings m_characterTouches up to date from each character's
         * end-of-move contacts and its body's step contacts, adding the begins and ends of the pairs reported. */
        void TrackCharacterContacts(std::vector<ContactBegin>& begins, std::vector<ContactEnd>& ends);

        // Debug lines (DebugLines.cpp): remade after each step from the categories switched on, if any.
        void DrawDebugLines();
        struct ShapeEdgesKey;
        void DrawColliders(std::map<ShapeEdgesKey, std::vector<Math::Vec3>>& used);
        void DrawJoints();
        void DrawCharacters(std::map<ShapeEdgesKey, std::vector<Math::Vec3>>& used);
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
        ComponentWatch m_jointWatch;

        // The entities with each component now, as the watches have reported them.
        std::set<std::uint32_t> m_rigidBodyEntities;
        std::set<std::uint32_t> m_colliderEntities;

        // RigidBodies with no collider: inactive in physics. Each is warned about once, when it becomes one.
        std::set<std::uint32_t> m_empty;

        // By the body's entity (its RigidBody's, or a static collider's own), so bodies are made, diffed and written
        // back in a reproducible order (ADR-0011).
        std::map<std::uint32_t, Record> m_bodies;

        // What physics last applied for a joint: the diff compares the Joint against it. A joint naming no made body
        // entity has no constraint.
        struct JointRecord
        {
            [[nodiscard]] bool HasConstraint() const
            {
                return id.value != Backend::JointId{}.value;
            }

            Backend::JointId id;
            Joint applied;
            // The bodies' world scales and backend bodies the constraint was made with.
            Math::Vec3 scaleA = Math::Vec3(1.0f);
            Math::Vec3 scaleB = Math::Vec3(1.0f);
            Backend::BodyId bodyA;
            Backend::BodyId bodyB;
            // The kinds of out-of-range value already warned about, so each is warned about once per joint.
            std::uint32_t warned = 0;
            // What the constraint applied to bodyB in the last step its bodies were awake for.
            Backend::JointLoad load;
            // Inactive: it names no pair of made body entities. Warned about each time it becomes so.
            bool inactive = false;
        };

        // The entities with a Joint now, as the watch has reported them, and their records, by joint entity: joints
        // are made, diffed and solved in that order.
        std::set<std::uint32_t> m_jointEntities;
        std::map<std::uint32_t, JointRecord> m_joints;
        // The joint entities whose constraints join each backend body, by its BodyId: removed before the body is.
        std::map<std::uint32_t, std::set<std::uint32_t>> m_jointsOfBody;
        // The joints the last step broke, sent with its contact and trigger events.
        std::vector<JointBroken> m_broken;

        // What physics knows of a character body: its backend character, where its last move left it, and the shape and
        // world scale it has, which the diff compares the Collider and Transform against. None for one disabled, or
        // inactive (beside a RigidBody, with no Collider, or with a shape a character cannot use).
        struct CharacterRecord
        {
            Backend::CharacterId id;
            Backend::CharacterMoved moved;
            // Its world transform as physics last applied it or wrote it back: a position differing from it is a
            // teleport, and a rotation differing from it a turn.
            Transform world;
            Shown shown{};
            CollisionShape shape;
            Math::Vec3 scale = Math::Vec3(1.0f);
            // Its entity's PhysicsMaterial and CollisionFilter as last applied to its body.
            PhysicsMaterial material;
            CollisionFilter filter;
            // The Collider's shape or scale does not fit yet: the character keeps `shape` and tries again each move.
            bool shapeBlocked = false;
        };
        ComponentWatch m_characterWatch;
        // The entities with a CharacterBody now, as the watch has reported them, and their records, by entity:
        // characters are made and moved in that order.
        std::set<std::uint32_t> m_characterEntities;
        std::map<std::uint32_t, CharacterRecord> m_characters;
        // CharacterBodies that cannot be characters now (beside a RigidBody, with no Collider, or with a shape a
        // character cannot use), and solid colliders beneath a character: each is warned about once, when it becomes
        // one.
        std::set<std::uint32_t> m_inactiveCharacters;
        std::set<std::uint32_t> m_characterChildren;

        // What physics knows of a vehicle, by chassis entity: its backend vehicle, the body and wheel entities it was
        // made with, and the driver input last given to it. None while its chassis has no dynamic body or no wheels.
        struct VehicleRecord
        {
            [[nodiscard]] bool Made() const
            {
                return id.value != Backend::VehicleId{}.value;
            }

            Backend::VehicleId id;
            Backend::BodyId body;
            // In wheel order (entity order), with each wheel's position as given to the backend.
            std::vector<std::uint32_t> wheels;
            std::vector<Math::Vec3> positions;
            VehicleInput input;
        };
        ComponentWatch m_vehicleWatch;
        ComponentWatch m_wheelWatch;
        // The entities with a Vehicle, and with a VehicleWheel, now, as the watches have reported them.
        std::set<std::uint32_t> m_vehicleEntities;
        std::set<std::uint32_t> m_wheelEntities;
        std::map<std::uint32_t, VehicleRecord> m_vehicles;

        // What game code has been told is touching, so every begin and enter gets its end and exit.
        std::set<ContactKey> m_reportedContacts;
        // The pairs with a character touching after the last step, whether reported or not, oriented as their key:
        // touching while either the character's last move or its body's step finds them.
        std::map<ContactKey, Backend::Touch> m_characterTouches;
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
