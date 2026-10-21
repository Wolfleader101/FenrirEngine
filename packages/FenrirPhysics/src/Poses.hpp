#pragma once

// The physics side of the Transforms physics writes for bodies and characters: for each, the pose physics last applied
// or wrote back (the baseline game code's edits are judged against), each step's poses written back parents before
// children, the last two step poses, and Restore and Blend between ticks. Bodies and the character code push their
// edits through it and hand it their poses; it reads neither. Knows the ECS, not Jolt. Private to the FenrirPhysics
// package.

#include <cstdint>
#include <map>
#include <optional>
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
        /** @brief What an entity whose Transform physics writes is, for judging its edits, writing it back and showing
         * it between ticks. */
        enum class Kind
        {
            // A dynamic body: its step pose comes from the step, is written to its Transform, and is shown between
            // ticks.
            Written,
            // A kinematic body or a trigger: game code places it, the step moves it there, and it is shown between
            // ticks.
            Followed,
            // A static or inactive body: never shown between ticks.
            Still,
            // A character: only its position is physics's. Its rotation and scale stay game code's, and a turn is not
            // a teleport.
            Character,
        };

        /** @brief What game code did to an entity's Transform since physics last put a pose there. */
        enum class Edit
        {
            None,
            // Its world position changed (a moved parent included).
            Moved,
            // Its world rotation changed, and not its position.
            Turned,
        };

        /** @brief The poses of the bodies and characters in `entities`. */
        explicit Poses(EntityList& entities);

        /** @brief A new body or character on `entity`, made at world transform `world`: it starts afresh, `world` its
         * baseline, and snaps at its next step. */
        void Made(std::uint32_t entity, const Transform& world);

        /** @brief The record of the body or character on `entity` is gone: a new one on it starts afresh. */
        void Forget(std::uint32_t entity);

        /**
         * @brief Edits: what game code did to the Transform of `entity`, a `kind`, now at world transform `world`,
         * judged against its baseline, which `world` then becomes. A move snaps it at its next step, and so does a turn
         * unless it is a character; a followed body never snaps, as the step moves it to where it was put.
         */
        Edit Push(std::uint32_t entity, Kind kind, const Transform& world);

        /** @brief Write-back: the step pose of `entity`, a `kind`, is `world`, written to its Transform in WriteBack
         * unless it is a followed or still body; a character's is only its position. Every body and character hands
         * its pose each step. */
        void Hand(std::uint32_t entity, Kind kind, const Transform& world);

        /** @brief Write-back: writes the handed poses to their Transforms, parents before children, bodies and
         * characters in one order, and reads each back as its baseline. */
        void WriteBack();

        /** @brief The world transform physics last applied to the body or character on `entity` or wrote back from it,
         * read back the way the diff reads, so rounding through a parent is not taken for a game edit. */
        [[nodiscard]] const Transform& Baseline(std::uint32_t entity) const;

        /** @brief The world pose of the body on `entity` after the last step, never its interpolated Transform; a
         * default Transform if it has had no step yet. */
        [[nodiscard]] const Transform& StepPose(std::uint32_t entity) const;

        /** @brief Whether the body on `entity` snaps at the next Advance: after the write-back, whether this step
         * teleported it or it is new. */
        [[nodiscard]] bool Snaps(std::uint32_t entity) const;

        /** @brief The pose of the body on `entity` `fraction` of the way from its previous step pose to its current
         * one, with its current scale, if Blend wrote its Transform this frame; nothing otherwise. */
        [[nodiscard]] std::optional<Transform> BlendedPose(std::uint32_t entity, float fraction) const;

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
        // How a body or character is shown between ticks: its world poses after the step before last and after the
        // last step. A teleport or a new one snaps: previous = current after its next step, so nothing blends across
        // the jump.
        struct Shown
        {
            // After a step that left it at `world`.
            void Advance(const Transform& world);
            // `BeforeTicks`, for one Blend wrote to `local`: put back the true pose, but for the parts game code
            // changed since, which a step takes as a teleport if it moved it, or if `turnIsTeleport`.
            void Restore(Transform& local, bool turnIsTeleport);
            // Its world pose `fraction` of the way from its previous step pose to its current one, with its current
            // scale.
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

        struct Entry
        {
            Kind kind = Kind::Still;
            // The world transform physics last applied or wrote back, which game code's edits are judged against.
            // After the write-back it is the step pose.
            Transform baseline;
            Shown shown{};
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
        // By entity, in entity order.
        std::map<std::uint32_t, Entry> m_entries;
        std::vector<Pending> m_pending;
    };
} // namespace Fenrir::Detail
