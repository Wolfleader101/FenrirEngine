#pragma once

// The Transforms physics writes for bodies and characters, and how they are shown between ticks: each step's poses
// written back parents before children, the last two step poses of each, and Restore and Blend between ticks. Bodies
// and the character code hand it their poses; it reads neither. Knows the ECS, not Jolt. Private to the FenrirPhysics
// package.

#include <cstdint>
#include <map>
#include <vector>

#include "FenrirECS/DefaultComponents.hpp"

namespace Fenrir
{
    class EntityList;
} // namespace Fenrir

namespace Fenrir::Detail
{
    class Poses
    {
      public:
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
            /** @brief Its world pose `fraction` of the way from its previous step pose to its current one, with its
             * current scale. */
            [[nodiscard]] Transform Between(float fraction) const;

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

        /** @brief What a body's step pose is, for writing it back and showing it between ticks. */
        enum class BodyPose
        {
            // A dynamic body's, from the step: written to its Transform, and shown between ticks.
            Written,
            // A kinematic body's or a trigger's, where game code put it: shown between ticks.
            Followed,
            // A static or inactive body's: never shown between ticks.
            Still,
        };

        /** @brief The poses of the bodies and characters in `entities`. */
        explicit Poses(EntityList& entities);

        /** @brief Write-back: the step pose of the body whose entity is `entity`, `world`, written to its Transform in
         * WriteBack if `pose` is Written. Every body hands its pose each step. */
        void HandBody(std::uint32_t entity, const Transform& world, BodyPose pose);

        /** @brief Write-back: the character on `entity` is at `position` after its move, written in WriteBack. Its
         * rotation and scale stay game code's. Every character hands its position each step. */
        void HandCharacter(std::uint32_t entity, const Math::Vec3& position);

        /** @brief Write-back: writes the handed poses to their Transforms, parents before children, bodies and
         * characters in one order. */
        void WriteBack();

        /** @brief A teleport: the body whose entity is `entity` snaps at its next step. */
        void SnapBody(std::uint32_t entity);
        /** @brief A teleport: the character on `entity` snaps at its next step. */
        void SnapCharacter(std::uint32_t entity);

        /** @brief The body's record is gone: a new body on `entity` starts afresh. */
        void ForgetBody(std::uint32_t entity);
        /** @brief The character's record is gone: a new character on `entity` starts afresh. */
        void ForgetCharacter(std::uint32_t entity);

        /** @brief The world transform of the body whose entity is `entity` after the last WriteBack wrote it, read
         * back the way the diff reads, so rounding through a parent is not taken for a game edit. */
        [[nodiscard]] const Transform& BodyStepPose(std::uint32_t entity) const;
        /** @brief As BodyStepPose, for the character on `entity`. */
        [[nodiscard]] const Transform& CharacterStepPose(std::uint32_t entity) const;

        /** @brief How the body whose entity is `entity` is shown between ticks; a new one's, if it has none yet. */
        [[nodiscard]] const Shown& BodyShown(std::uint32_t entity) const;

        /** @brief After the step's events: each body's and character's last two step poses move on by one. */
        void Advance();

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

      private:
        struct Entry
        {
            Shown shown{};
            // Its step pose, as handed, or read back after WriteBack wrote it.
            Transform step;
            BodyPose pose = BodyPose::Still;
        };

        // A pose to write back.
        struct Pending
        {
            int depth;
            std::uint32_t entity;
            bool isCharacter;
            Transform world;
        };

        EntityList* m_entities;
        // By entity, in entity order: Restore and Blend go through the bodies, then the characters.
        std::map<std::uint32_t, Entry> m_bodies;
        std::map<std::uint32_t, Entry> m_characters;
        std::vector<Pending> m_pending;
    };
} // namespace Fenrir::Detail
