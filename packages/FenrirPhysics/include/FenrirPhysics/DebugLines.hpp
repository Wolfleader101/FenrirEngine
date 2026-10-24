#pragma once

#include <cstdint>
#include <string_view>

#include "FenrirMath/Colour.hpp"

namespace Fenrir
{
    /**
     * @brief The debug group a physics world draws its debug lines into, in its scene's DebugDraw (FenrirDebugDraw,
     * ADR-0025): hide it there to hide physics's lines without stopping them being drawn.
     */
    inline constexpr std::string_view PhysicsDebugGroup = "Physics";

    /**
     * @brief Which categories of debug lines a physics world draws (PhysicsWorld::SetDebugDraw). Bit flags. After each
     * step, the world draws the categories chosen into its scene's DebugDraw, in the PhysicsDebugGroup, with duration
     * 0: each step's lines last until the next tick.
     */
    enum class PhysicsDebugCategories : std::uint32_t
    {
        None = 0,
        /** @brief Each collider's wireframe at its body's pose, coloured by motion type (PhysicsDebugColour). */
        Colliders = 1u << 0,
        /** @brief Each contact between solid bodies: a small cross at its point and a short line along its normal. */
        Contacts = 1u << 1,
        /** @brief A line from each moving body's position along its linear velocity, as far as it travels in a tenth
           of a second. */
        Velocities = 1u << 2,
        /** @brief Each body's world-space bounding box (PhysicsWorld::Bounds). */
        Bounds = 1u << 3,
        /** @brief Each joint's two frames as axes, X, Y and Z, at their bodies' step poses: frameA long (JointAX,
           JointAY, JointAZ) and frameB short (JointBX, JointBY, JointBZ). They coincide at the rest pose and while the
           joint holds. A disabled joint (broken, or disabled by game code) is drawn Dimmed. An inactive joint draws
           only the frames on its bodies that are still bodies, in InactiveJoint grey; never its world end. */
        Joints = 1u << 4,
        /** @brief Each active character, at its step pose: the shape it uses in the Character colour (the old one while
           a shape edit waits, with `state.shapeBlocked` set: the shape bodies and queries see), a short line along its
           ground normal from its ground point while on ground (Ground, or SteepGround), and a line along its
           `state.movedVelocity` as far as it travels in a tenth of a second, as Velocities draws a body's. Colliders
           never draws a character. */
        Characters = 1u << 5,
        /** @brief Each wheel of each active, enabled vehicle, at its step pose: its suspension, a line in the
           Suspension colour from the top of its travel down to its full-extension point (VehicleWheel::position), with
           a short cross-tick at the wheel's centre; the wheel, a circle of its radius in its plane in the Wheel colour,
           with one spoke so its spin and steer show; and while on ground, a small cross at its contact point and a
           short line along the contact normal, coloured by its grip: Gripping, PastPeakSlip (either slip past its
           tyre's `peakSlip`) or PastSlideSlip (either past its `slideSlip`). Probe shapes are not drawn. Disabled and
           inactive vehicles draw nothing; Colliders still draws their chassis. */
        Vehicles = 1u << 6,
        All = Colliders | Contacts | Velocities | Bounds | Joints | Characters | Vehicles
    };

    /** @brief The categories in `a` or `b`. */
    [[nodiscard]] constexpr PhysicsDebugCategories operator|(PhysicsDebugCategories a, PhysicsDebugCategories b)
    {
        return static_cast<PhysicsDebugCategories>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
    }

    /** @brief The categories in both `a` and `b`. */
    [[nodiscard]] constexpr PhysicsDebugCategories operator&(PhysicsDebugCategories a, PhysicsDebugCategories b)
    {
        return static_cast<PhysicsDebugCategories>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
    }

    /** @brief Whether `categories` includes any of `wanted`. */
    [[nodiscard]] constexpr bool Includes(PhysicsDebugCategories categories, PhysicsDebugCategories wanted)
    {
        return (categories & wanted) != PhysicsDebugCategories::None;
    }

    /** @brief The fixed colours of a physics world's debug lines, written in sRGB. */
    namespace PhysicsDebugColour
    {
        /** @brief A static body's collider. */
        inline const Colour Static = Colour::FromSrgb(0x9E9E9EFFu);
        /** @brief A kinematic body's collider. */
        inline const Colour Kinematic = Colour::FromSrgb(0x4DA6FFFFu);
        /** @brief A dynamic body's collider. */
        inline const Colour Dynamic = Colour::FromSrgb(0x4DE64DFFu);
        /** @brief A trigger's collider, whatever body it is on. */
        inline const Colour Trigger = Colour::FromSrgb(0xFF66E6FFu);
        /** @brief A contact's point and normal. */
        inline const Colour Contact = Colour::FromSrgb(0xFF3333FFu);
        /** @brief A body's velocity. */
        inline const Colour Velocity = Colour::FromSrgb(0xFFE633FFu);
        /** @brief A body's bounding box. */
        inline const Colour Bounds = Colour::FromSrgb(0xFF9933FFu);
        /** @brief A joint's frameA: its X axis (red), Y axis (green) and Z axis (blue). */
        inline const Colour JointAX = Colour::FromSrgb(0xE6194BFFu);
        inline const Colour JointAY = Colour::FromSrgb(0x3CB44BFFu);
        inline const Colour JointAZ = Colour::FromSrgb(0x4363D8FFu);
        /** @brief A joint's frameB: its X axis (pink), Y axis (yellow) and Z axis (cyan). */
        inline const Colour JointBX = Colour::FromSrgb(0xFF99CCFFu);
        inline const Colour JointBY = Colour::FromSrgb(0xFFFF4DFFu);
        inline const Colour JointBZ = Colour::FromSrgb(0x4DFFFFFFu);
        /** @brief Every axis of an inactive joint's frames, drawn only on its bodies that are still bodies. */
        inline const Colour InactiveJoint = Colour::FromSrgb(0x6B6B6BFFu);
        /** @brief A character's shape in use. */
        inline const Colour Character = Colour::FromSrgb(0xF2F2F2FFu);
        /** @brief A character's ground normal on walkable ground (GroundState::OnGround). */
        inline const Colour Ground = Colour::FromSrgb(0x33E6B3FFu);
        /** @brief A character's ground normal on ground too steep to walk (GroundState::OnSteepGround). */
        inline const Colour SteepGround = Colour::FromSrgb(0xB366FFFFu);
        /** @brief A vehicle wheel's suspension and its cross-tick. */
        inline const Colour Suspension = Colour::FromSrgb(0x8C8CFFFFu);
        /** @brief A vehicle wheel's circle and spoke. */
        inline const Colour Wheel = Colour::FromSrgb(0xD9B380FFu);
        /** @brief A vehicle wheel's contact while both its slips are within its tyres' `peakSlip`. */
        inline const Colour Gripping = Colour::FromSrgb(0x33FF33FFu);
        /** @brief A vehicle wheel's contact while either slip is past its tyre's `peakSlip`, and neither past its
         * `slideSlip`. */
        inline const Colour PastPeakSlip = Colour::FromSrgb(0xFFC21AFFu);
        /** @brief A vehicle wheel's contact while either slip is past its tyre's `slideSlip`. */
        inline const Colour PastSlideSlip = Colour::FromSrgb(0xFF1A8CFFu);

        /**
         * @brief A sleeping body's collider colour, or a disabled joint's axis colour: `colour` at half brightness as
         * sRGB counts it, so each channel scaled by mid grey's linear value (Colour::Grey), alpha unchanged.
         */
        [[nodiscard]] inline Colour Dimmed(const Colour& colour)
        {
            const float half = Colour::Grey.r;
            return {colour.r * half, colour.g * half, colour.b * half, colour.a};
        }
    } // namespace PhysicsDebugColour
} // namespace Fenrir
