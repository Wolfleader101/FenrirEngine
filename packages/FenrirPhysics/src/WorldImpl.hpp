#pragma once

// A physics world's private state and its PostTick pipeline. Knows the ECS, not Jolt: it talks to Backend::World.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Backend/Backend.hpp"
#include "Bodies.hpp"
#include "DebugCanvas.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/TrackedComponents.hpp"
#include "FenrirPhysics/Characters.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Events.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirPhysics/Vehicles.hpp"
#include "FenrirScene/Scene.hpp"
#include "Poses.hpp"

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
         * character only if its position changed. Wheels go back to their true poses whatever game code wrote.
         */
        void Restore();

        /**
         * @brief `AfterTicks`: write each interpolated dynamic or kinematic body's and character's pose `fraction` of
         * the way from its previous step pose to its current one, parents before children. One that is not
         * interpolated, beneath one that is, is written at its step pose, so its parent's blend does not carry it.
         * Then each interpolated chassis's wheels, blended with it (BlendWheels).
         */
        void Blend(float fraction);

        using Pusher = Detail::Bodies::Pusher;

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
        void Queue(const Entity& entity, Pusher pusher, const Math::Vec3& amount)
        {
            m_bodies.Queue(entity, pusher, amount);
        }

      private:
        // The queries, in Queries.cpp, read the bodies and the backend.
        friend class PhysicsWorld;

        using Record = Detail::Bodies::Record;

        /** @brief What the features not yet in modules of their own (joints, vehicles, contacts) and the queries read
         * of the bodies. */
        [[nodiscard]] const Detail::Bodies& BodyView() const
        {
            return m_bodies;
        }

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

        // Joints (Joints.cpp). Their constraints always go before their bodies and come after them.

        /** @brief Drains the Joint tracked set, after the bodies' plan: a joint removed (or removed and added again)
         * since the last tick loses its constraint, and one added and removed again comes to nothing. */
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

        // Character bodies (Characters.cpp): each a backend character, made after the bodies' batch and destroyed
        // before it.

        /** @brief Brings the CharacterBody tracked set up to date, then destroys the backend characters of the
         * entities in `changed`, whose body components came or went since the last tick (they are made again from what
         * the entity has now), and of those disabled or gone inactive. */
        void DropCharacters(const std::set<std::uint32_t>& changed);
        /** @brief Warns once about each CharacterBody that has gone inactive, then makes, in one batch in entity order,
         * the backend character of each enabled one that can have one and has none. */
        void MakeCharacters();
        /** @brief Edits, after the bodies', in entity order: its entity's PhysicsMaterial and CollisionFilter
         * given to its body if they changed; a character whose world position differs from physics's copy is
         * teleported there (a moved parent included), and one whose world rotation alone differs is turned. Neither
         * sweeps. */
        void PushCharacterEdits();
        /** @brief Moves, before the step: applies each character's shape edit if it fits, then moves it by its
         * velocity over `dt` seconds, one at a time in entity order, keeping each move's result for WriteBack. */
        void MoveCharacters(float dt);
        struct CharacterRecord;
        /** @brief Moves, for one character before its move: gives it its Collider's shape and world scale if they
         * changed and fit, keeping the feet in place on the ground and the head in the air; otherwise marks it
         * blocked, keeping the shape it has. */
        void ApplyShape(const Entity& entity, CharacterRecord& record);
        /** @brief Write-back, before Poses writes the poses: hands each character's position from its last move to
         * Poses. */
        void HandCharacterPoses();
        /** @brief Write-back, after Poses wrote the poses, for one character: its trimmed velocity and state, from its
         * last move, whether its Collider's shape or scale is still waiting to fit, and the world transform the diff
         * compares against. */
        void ReadBackCharacter(Entity& entity, CharacterRecord& record);
        /** @brief Write-back, for each CharacterBody with no backend character (disabled or inactive): its state reset
         * to the defaults, and nothing else written. */
        void ResetIdleCharacters();

        // Vehicles (Vehicles.cpp): each a backend vehicle on its chassis's body, removed before its body goes, made
        // after the joints in the edits, and written back after the bodies in the write-back.

        /** @brief Drains the Vehicle, VehicleWheel, VehicleDifferential and VehicleAntiRollBar tracked sets, after the
         * bodies' plan: a vehicle whose Vehicle or one of whose wheels came or went since the last tick loses its
         * backend vehicle, made again in the edits. */
        void DrainVehicles();
        /** @brief Removes the backend vehicles on any of `bodies`, which are about to be destroyed. */
        void RemoveVehiclesOf(std::span<const Backend::BodyId> bodies);
        struct VehicleRecord;
        /** @brief Removes the vehicle's backend vehicle, if it has one, and forgets what was applied to it. For a
         * rebuild
         * (`carryOver`), its engine speed, gear and wheel spins are kept for the vehicle made next, or kept from an
         * earlier rebuild's drop if it has none; otherwise the vehicle made next starts fresh. */
        void DropVehicle(VehicleRecord& record, bool carryOver);
        /** @brief Why the Vehicle on `chassis` is inactive: no made dynamic body with a solid collider, or beside a
         * CharacterBody. Empty if it can be a vehicle. */
        [[nodiscard]] std::string WhyNoChassis(std::uint32_t chassis) const;
        /** @brief Why the VehicleWheel on `wheel` is inactive: naming no other entity of its scene with a Vehicle, or
         * beside a RigidBody, CharacterBody or Collider. Empty if it can be a wheel. */
        [[nodiscard]] std::string WhyNoWheel(std::uint32_t wheel) const;
        /** @brief Warns once about each Vehicle and each VehicleWheel that has gone inactive since the last tick. */
        void WarnAboutInactiveVehicles();
        /** @brief The made dynamic body of the chassis on `chassis`, if it can be a vehicle's; null otherwise. */
        [[nodiscard]] const Record* ChassisBodyOf(std::uint32_t chassis) const;
        /** @brief Each vehicle's wheel entities that can be its wheels, in entity order, by chassis entity. */
        [[nodiscard]] std::map<std::uint32_t, std::vector<std::uint32_t>> WheelsByVehicle() const;
        /** @brief Edits, after the joints, in chassis entity order: warns about vehicles and wheels gone inactive,
         * makes the backend vehicle of each enabled chassis with a dynamic body and wheels, remade when its body or
         * wheel set changed, and gives it its differentials, anti-roll bars and driver input, waking the chassis on a
         * change. */
        void SyncVehicles();
        /** @brief The backend's wheel for the wheel on `entity`, its settings corrected (each kind of out-of-range
         * value warned about once per wheel) and its position scaled by `scale`, the chassis's world scale. */
        [[nodiscard]] Backend::VehicleWheelDesc WheelDesc(std::uint32_t entity, const VehicleWheel& wheel,
                                                          const Math::Vec3& scale);
        /** @brief Patches a made vehicle's edited wheels, probe and tilt limit into its backend vehicle; true if
         * anything changed. */
        bool SyncVehicleSettings(VehicleRecord& record, const Vehicle& vehicle, const Math::Vec3& scale);
        /** @brief A wheel's pose relative to its chassis's origin, in the chassis's axes: its position raised up its
         * steering axis by its compression, turned by its camber and toe, steered about its steering axis, then spun
         * about its axle, X. */
        [[nodiscard]] static Transform WheelPose(const Backend::VehicleWheelDesc& wheel,
                                                 const Backend::VehicleWheelRead& read);
        /** @brief Writes each of the vehicle's wheel entities' world position and rotation from `read`, against the
         * chassis pose `chassis`, parents before children. */
        void PoseWheels(const VehicleRecord& record, const Backend::VehicleRead& read, const Transform& chassis);
        /** @brief Write-back, after the bodies, for a step of `dt` seconds: each vehicle's state, its wheels' states
         * and their poses, and how its wheels are shown between ticks; the states of those with no backend vehicle
         * reset. A sleeping vehicle keeps the state it fell asleep with, its wheels' spin 0. */
        void WriteBackVehicles(float dt);
        /** @brief After a step of `dt` seconds left the vehicle's wheels as `read`: their last two steps for
         * BlendWheels, the spin unwrapped. A new vehicle, or one whose chassis was `teleported`, snaps. */
        void AdvanceWheels(VehicleRecord& record, const Backend::VehicleRead& read, float dt, bool teleported);
        /** @brief Part of Blend, after the bodies: poses the wheels of each vehicle whose chassis Blend blended,
         * `fraction` of the way between their last two steps in the chassis's axes, on the chassis's blended pose. */
        void BlendWheels(float fraction);
        /** @brief Part of Restore: puts back the true pose of each wheel BlendWheels wrote. */
        void RestoreWheels();
        // A vehicle's differentials and anti-roll bars, by wheel index, as they are to be given to its backend vehicle.
        struct VehicleLinks
        {
            std::vector<Backend::VehicleDifferentialDesc> differentials;
            std::vector<Backend::VehicleAntiRollBarDesc> antiRollBars;
        };
        /** @brief Each vehicle's differentials and anti-roll bars, in entity order, by chassis entity, from `wheelsOf`
         * (WheelsByVehicle): out-of-range values corrected and those naming wrong wheels inactive, each with a
         * warning. */
        [[nodiscard]] std::map<std::uint32_t, VehicleLinks> LinksByVehicle(
            const std::map<std::uint32_t, std::vector<std::uint32_t>>& wheelsOf);

        /** @brief After the step: dynamic bodies' and characters' poses written back through Poses, parents before
         * children, their velocities and the characters' states, then the state of each character with no backend
         * character reset. */
        void WriteBack();
        void SendEvents(App& app);
        /** @brief Events, before the events are sorted: brings m_characterTouches up to date from each character's
         * end-of-move contacts and its body's step contacts, adding the begins and ends of the pairs reported. */
        void TrackCharacterContacts(std::vector<ContactBegin>& begins, std::vector<ContactEnd>& ends);

        // Debug lines (DebugLines.cpp): remade after each step from the categories switched on, if any.
        void DrawDebugLines();
        void DrawJoints();
        void DrawCharacters();
        void DrawVehicles();
        void DrawContacts();

        [[nodiscard]] bool ReportsContacts(std::uint32_t body) const;

        /** @brief The backend's form of a query's filter: its ignored body resolved as ResolveBody does, and a handle
         * to an entity no longer alive ignoring nothing. */
        [[nodiscard]] Backend::QueryFilter ToBackend(const QueryFilter& filter) const;

        EntityList* m_entities;
        ILogger* m_logger;
        Backend::World m_backend;
        Detail::Poses m_poses;
        Detail::Bodies m_bodies;

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

        // The entities with a Joint, as of the last DrainJoints, and their records, by joint entity: joints are made,
        // diffed and solved in that order.
        TrackedComponents m_jointEntities;
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
            CollisionShape shape;
            Math::Vec3 scale = Math::Vec3(1.0f);
            // Its entity's PhysicsMaterial and CollisionFilter as last applied to its body.
            PhysicsMaterial material;
            CollisionFilter filter;
            // The Collider's shape or scale does not fit yet: the character keeps `shape` and tries again each move.
            bool shapeBlocked = false;
        };
        // The entities with a CharacterBody, as of the last DropCharacters, and their records, by entity: characters
        // are made and moved in that order.
        TrackedComponents m_characterEntities;
        std::map<std::uint32_t, CharacterRecord> m_characters;
        // CharacterBodies that cannot be characters now (beside a RigidBody, with no Collider, or with a shape a
        // character cannot use): each is warned about once, when it becomes one.
        std::set<std::uint32_t> m_inactiveCharacters;

        // What physics knows of a vehicle, by chassis entity: its backend vehicle, the body and wheel entities it was
        // made with, and the driver input and kind last given to it. None while it is disabled or inactive, or has no
        // wheels.
        struct VehicleRecord
        {
            [[nodiscard]] bool Made() const
            {
                return id.value != Backend::VehicleId{}.value;
            }

            Backend::VehicleId id;
            Backend::BodyId body;
            // In wheel order (entity order), with each wheel's settings as last applied and as given to the backend.
            std::vector<std::uint32_t> wheels;
            std::vector<VehicleWheel> appliedWheels;
            std::vector<Backend::VehicleWheelDesc> given;
            VehicleProbe probe = VehicleProbe::Cylinder;
            float maxTilt = 0.0f;
            // The driver input as game code last wrote it: any change wakes the chassis.
            VehicleInput input;
            // The differentials and anti-roll bars last given to it.
            VehicleLinks links;
            // The kind as game code last gave it, to notice its edits, and as applied, in range.
            VehicleKind kind;
            VehicleKind applied;
            // The kinds of out-of-range value its kind holds and has been warned about: each is warned about once per
            // vehicle until a valid value clears it.
            std::uint32_t warned = 0;
            // Dropped for a rebuild: what the vehicle made next carries over, each wheel's spin by its entity.
            struct Carry
            {
                float engineRpm = 0.0f;
                int gear = 0;
                float clutch = 0.0f;
                std::map<std::uint32_t, Backend::VehicleWheelSpin> wheels;
            };
            std::optional<Carry> carry;
            // Asleep: the read the step it fell asleep left, its wheels still, kept until it wakes.
            std::optional<Backend::VehicleRead> parked;

            // How its wheels are shown between ticks, in the chassis's axes, in wheel order: as the step before last
            // and the last step left them, the spin unwrapped. A new vehicle, or its chassis teleported, snaps: the
            // two are the same after its next step, so nothing blends across the jump.
            struct WheelShown
            {
                float compression = 0.0f;
                float steer = 0.0f;
                float spin = 0.0f;
            };
            std::vector<WheelShown> previous;
            std::vector<WheelShown> current;
            bool snap = true;
            // Whether BlendWheels wrote the wheel entities' Transforms, and each wheel entity's true local Transform
            // it replaced.
            bool blended = false;
            std::vector<std::pair<Entity, Transform>> trueLocals;
        };
        // The entities with a Vehicle, and with a VehicleWheel, as of the last DrainVehicles.
        TrackedComponents m_vehicleEntities;
        TrackedComponents m_wheelEntities;
        std::map<std::uint32_t, VehicleRecord> m_vehicles;
        // Vehicles and wheels that are inactive now: each is warned about once, when it becomes so.
        std::set<std::uint32_t> m_inactiveVehicles;
        std::set<std::uint32_t> m_inactiveWheels;
        // What physics knows of a VehicleDifferential or VehicleAntiRollBar, by its entity: whether it is inactive,
        // warned about each time it becomes so, and the kinds of out-of-range value already warned about, each once.
        struct VehicleLinkRecord
        {
            bool inactive = false;
            std::uint32_t warned = 0;
            // The entities it names that have been wheels while named, so one that loses its VehicleWheel is a dead
            // name rather than a wrong one; and its dead names, each warned about once.
            std::set<std::uint32_t> wheels;
            std::set<std::uint32_t> dead;
        };
        // The records are kept for the entities in their tracked sets.
        TrackedComponents m_differentialEntities;
        TrackedComponents m_antiRollBarEntities;
        std::map<std::uint32_t, VehicleLinkRecord> m_differentials;
        std::map<std::uint32_t, VehicleLinkRecord> m_antiRollBars;
        // The kinds of out-of-range value each wheel entity has been warned about, so each is warned about once.
        std::map<std::uint32_t, std::uint32_t> m_wheelWarnings;

        // What game code has been told is touching, so every begin and enter gets its end and exit.
        std::set<ContactKey> m_reportedContacts;
        // The pairs with a character touching after the last step, whether reported or not, oriented as their key:
        // touching while either the character's last move or its body's step finds them.
        std::map<ContactKey, Backend::Touch> m_characterTouches;
        std::set<OverlapKey> m_reportedOverlaps;

        // The debug lines switched on, and those the last step made.
        DebugDraw m_debugDraw = DebugDraw::None;
        Detail::DebugCanvas m_canvas;
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
