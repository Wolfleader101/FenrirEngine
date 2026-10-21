#pragma once

// A physics world's joints (ADR-0013): each Joint's constraint, made, recreated and updated from the diff against
// what physics last applied, removed before its bodies go, its load measured after each step, broken when overloaded,
// and its JointBroken sent. Reads the bodies through Bodies' const members and their step poses through Poses. Knows
// the ECS, not Jolt: it talks to Backend::World. Private to the FenrirPhysics package.

#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "Backend/Backend.hpp"
#include "Bodies.hpp"
#include "FenrirECS/TrackedComponents.hpp"
#include "FenrirPhysics/Events.hpp"
#include "FenrirPhysics/Joints.hpp"

namespace Fenrir
{
    class App;
    class Entity;
    class EntityList;
    class ILogger;
} // namespace Fenrir

namespace Fenrir::Detail
{
    class DebugCanvas;
    class Poses;

    class Joints
    {
      public:
        /** @brief The joints of `entities`, including those already there, made in `backend` between the bodies of
         * `bodies`, whose step poses `poses` keeps. */
        Joints(EntityList& entities, ILogger& logger, Backend::World& backend, const Bodies& bodies,
               const Poses& poses);

        /** @brief Drains the Joint tracked set, after the bodies' plan: a joint removed (or removed and added again)
         * since the last tick loses its constraint, and one added and removed again comes to nothing. */
        void Drain();

        /** @brief Removes the constraints of the joints naming any of `bodies`, which are about to be destroyed. */
        void DropOn(std::span<const Backend::BodyId> bodies);

        /** @brief After the bodies' edits: makes, recreates or updates each joint's constraint, in joint entity order,
         * with its settings corrected into range for a step of `dt` seconds. */
        void Sync(float dt);

        /** @brief After the step: each joint's load, kept from the last step either of its bodies was awake for, and 0
         * while it is disabled (JointState.cpp). */
        void Measure();

        /** @brief After Measure, in joint entity order: disables each enabled joint measured this step whose load is
         * over a finite threshold, in the backend, its Joint and its record, and queues a JointBroken for SendBroken
         * (JointBreaking.cpp). */
        void Break();

        /** @brief Events, right after the contact and trigger events: sends the JointBroken of each joint the step
         * broke to `app`, in joint entity order (JointBreaking.cpp). */
        void SendBroken(App& app);

        /** @brief PhysicsWorld::GetJointState: from each record's load and its bodies' poses after the last step. */
        [[nodiscard]] JointState StateOf(const Entity& joint) const;

        /** @brief Debug lines: each joint's two frames, dimmed while disabled, and an inactive joint's frames on the
         * bodies it still has, in grey. */
        void Draw(DebugCanvas& canvas) const;

      private:
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

        /** @brief The body `body` names, if it is a made body entity of this world; null otherwise. */
        [[nodiscard]] const Bodies::Body* BodyOf(const Entity& body) const;
        /** @brief Why `joint` names no pair of made body entities of this world, for its warning on going inactive. */
        [[nodiscard]] std::string WhyInactive(const Joint& joint) const;
        /** @brief Makes the constraint `desc` describes for the joint on `entity`, indexed under its bodies. */
        void MakeConstraint(std::uint32_t entity, JointRecord& record, const Backend::JointDesc& desc);
        /** @brief Removes the joint's constraint, if it has one, and its place in the index. */
        void DropConstraint(std::uint32_t entity, JointRecord& record);
        /** @brief Whether `body` names a made body that moves and is awake. */
        [[nodiscard]] bool IsAwake(const Entity& body) const;

        EntityList* m_entities;
        ILogger* m_logger;
        Backend::World* m_backend;
        const Bodies* m_bodies;
        const Poses* m_poses;

        // The entities with a Joint, as of the last Drain, and their records, by joint entity: joints are made,
        // diffed and solved in that order.
        TrackedComponents m_jointEntities;
        std::map<std::uint32_t, JointRecord> m_joints;
        // The joint entities whose constraints join each backend body, by its BodyId: removed before the body is.
        std::map<std::uint32_t, std::set<std::uint32_t>> m_jointsOfBody;
        // The joints the last step broke, sent right after its contact and trigger events.
        std::vector<JointBroken> m_broken;
    };
} // namespace Fenrir::Detail
