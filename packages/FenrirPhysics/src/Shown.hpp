#pragma once

// How a body or character is shown between ticks. Private to the FenrirPhysics package.

#include "FenrirECS/DefaultComponents.hpp"

namespace Fenrir::Detail
{
    // Its world poses after the step before last and after the last step. A teleport or a new one snaps: previous =
    // current after its next step, so nothing blends across the jump.
    struct Shown
    {
        /** @brief After a step that left it at `world`. */
        void Advance(const Transform& world);
        /** @brief `BeforeTicks`, for one Blend wrote to `local`: put back the true pose, but for the parts game code
         * changed since, which a step takes as a teleport if it moved it, or if `turnIsTeleport`. */
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
} // namespace Fenrir::Detail
