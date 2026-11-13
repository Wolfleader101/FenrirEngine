#pragma once

#include "FenrirMath/Colour.hpp"

namespace Fenrir
{
    /**
     * @brief The colours of FenrirRagdoll's debug lines, written in sRGB: the `PhysicsDebugCategories::Ragdolls`
     * category of a scene's physics world (PhysicsWorld::SetDebugDraw), drawn by the glue, not the world, into the
     * scene's DebugDraw in the PhysicsDebugGroup with duration 0, each frame after its `Update` work (ADR-0025).
     *
     * For each body of an active ragdoll that resolved last tick: its bone's frame, three axes 10 cm long from the
     * bone's origin along the bone's own axes in its simulation-pose world matrix, and a line from that origin to the
     * body's origin, all four in its effective mode's colour (so a simulating body's line shows how far physics has
     * taken it from the animation). For each joint the glue drives (its child body powered), the motor's target as a
     * frame 6 cm long at the joint: frameA on its parent body turned by the target, in DebugDraw::Axes's red, green
     * and blue, which frameB on the child reaches when the motor holds. An inactive ragdoll and a skipped body draw
     * nothing; joints and colliders keep their own categories.
     */
    namespace RagdollDebugColour
    {
        /** @brief A following body's bone frame and line. */
        inline const Colour Following = Colour::FromSrgb(0xBDBDBDFFu);
        /** @brief A simulating body's bone frame and line. */
        inline const Colour Simulating = Colour::FromSrgb(0xFF8C1AFFu);
        /** @brief A powered body's bone frame and line. */
        inline const Colour Powered = Colour::FromSrgb(0x2EB82EFFu);
    } // namespace RagdollDebugColour
} // namespace Fenrir
