#pragma once

// A physics world's character bodies (ADR-0014): each a backend character, destroyed before the bodies' batch and made
// after it, its edits pushed, moved before the step and its velocity and state written back after it, handing its pose
// to Poses. Knows the ECS, not Jolt: it talks to Backend::World. Private to the FenrirPhysics package.

#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <vector>

#include "Backend/Backend.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/TrackedComponents.hpp"
#include "FenrirPhysics/Characters.hpp"
#include "FenrirPhysics/Components.hpp"

namespace Fenrir
{
    class Entity;
    class EntityList;
    class ILogger;
} // namespace Fenrir

namespace Fenrir::Detail
{
    class DebugCanvas;
    class Poses;

    class Characters
    {
      public:
        /** @brief The character bodies of `entities`, including those already there, made in `backend`, their poses
         * written and shown through `poses`. */
        Characters(EntityList& entities, ILogger& logger, Backend::World& backend, Poses& poses);

        /** @brief Brings the CharacterBody tracked set up to date, then destroys the backend characters of the
         * entities in `changed`, whose body components came or went since the last tick (they are made again from what
         * the entity has now), and of those disabled or gone inactive. */
        void Drop(const std::set<std::uint32_t>& changed);

        /** @brief Warns once about each CharacterBody that has gone inactive, then makes, in one batch in entity order,
         * the backend character of each enabled one that can have one and has none. */
        void Make();

        /** @brief Edits, after the bodies', in entity order: its entity's PhysicsMaterial and CollisionFilter
         * given to its body if they changed; a character Poses finds moved since physics last put it somewhere is
         * teleported there (a moved parent included), and one it finds turned is turned. Neither sweeps. */
        void PushEdits();

        /** @brief Moves, before the step: applies each character's shape edit if it fits, then moves it by its
         * velocity over `dt` seconds, one at a time in entity order, keeping each move's result for the write-back. */
        void Move(float dt);

        /** @brief Write-back, before Poses writes the poses: hands each character's position from its last move to
         * Poses, and writes its trimmed velocity and state, from that move, and whether its Collider's shape or scale
         * is still waiting to fit. */
        void HandPoses();

        /** @brief Write-back, for each CharacterBody with no backend character (disabled or inactive): its state reset
         * to the defaults, and nothing else written. */
        void ResetIdle();

        /** @brief Each character's contacts at the end of its last move, in character entity order, for Contacts. */
        [[nodiscard]] std::vector<std::span<const Backend::Touch>> MoveContacts() const;

        /** @brief The contacts at the end of the last move of the character on `entity`; null if it has no backend
         * character. */
        [[nodiscard]] const std::vector<Backend::Touch>* MoveContactsOf(const Entity& entity) const;

        /** @brief Debug lines: each character's shape, its ground normal while it stands on ground, and its velocity
         * from its last move. */
        void Draw(DebugCanvas& canvas) const;

      private:
        // What physics knows of a character body: its backend character, where its last move left it, and the shape and
        // world scale it has, which the diff compares the Collider and Transform against (its world transform against
        // Poses' baseline). None for one disabled, or inactive (beside a RigidBody, with no Collider, or with a shape a
        // character cannot use).
        struct CharacterRecord
        {
            Backend::CharacterId id;
            Backend::CharacterMoved moved;
            CollisionShape shape;
            Math::Vec3 scale = Math::Vec3(1.0f);
            // Its entity's PhysicsMaterial and CollisionFilter as last applied to its body.
            PhysicsMaterial material;
            CollisionFilter filter;
            // The Collider's shape or scale does not fit yet: the character keeps `shape` and tries again each move.
            bool shapeBlocked = false;
        };

        /** @brief Moves, for one character before its move: gives it its Collider's shape and world scale if they
         * changed and fit, keeping the feet in place on the ground and the head in the air; otherwise marks it
         * blocked, keeping the shape it has. */
        void ApplyShape(const Entity& entity, CharacterRecord& record);

        EntityList* m_entities;
        ILogger* m_logger;
        Backend::World* m_backend;
        Poses* m_poses;

        // The entities with a CharacterBody, as of the last Drop, and their records, by entity: characters are made
        // and moved in that order.
        TrackedComponents m_characterEntities;
        std::map<std::uint32_t, CharacterRecord> m_characters;
        // CharacterBodies that cannot be characters now (beside a RigidBody, with no Collider, or with a shape a
        // character cannot use): each is warned about once, when it becomes one.
        std::set<std::uint32_t> m_inactiveCharacters;
    };
} // namespace Fenrir::Detail
