#pragma once

#include <cstdint>

#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    /**
     * @brief One line of a physics world's debug drawing, in world space: plain data any renderer can draw
     * (PhysicsWorld::DebugLines).
     */
    struct DebugLine
    {
        Math::Vec3 from = Math::Vec3(0.0f);
        Math::Vec3 to = Math::Vec3(0.0f);
        /** @brief Packed as 0xRRGGBBAA: red in the highest byte, alpha in the lowest. One of DebugColour's. */
        std::uint32_t colour = 0;
    };

    /** @brief Which categories of debug lines a physics world draws (PhysicsWorld::SetDebugDraw). Bit flags. */
    enum class DebugDraw : std::uint32_t
    {
        None = 0,
        /** @brief Each collider's wireframe at its body's pose, coloured by motion type (DebugColour). */
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
        All = Colliders | Contacts | Velocities | Bounds | Joints
    };

    /** @brief The categories in `a` or `b`. */
    [[nodiscard]] constexpr DebugDraw operator|(DebugDraw a, DebugDraw b)
    {
        return static_cast<DebugDraw>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
    }

    /** @brief The categories in both `a` and `b`. */
    [[nodiscard]] constexpr DebugDraw operator&(DebugDraw a, DebugDraw b)
    {
        return static_cast<DebugDraw>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
    }

    /** @brief Whether `categories` includes any of `wanted`. */
    [[nodiscard]] constexpr bool Includes(DebugDraw categories, DebugDraw wanted)
    {
        return (categories & wanted) != DebugDraw::None;
    }

    /** @brief The fixed colours of debug lines, packed as DebugLine::colour is. */
    namespace DebugColour
    {
        /** @brief A static body's collider. */
        inline constexpr std::uint32_t Static = 0x9E9E9EFFu;
        /** @brief A kinematic body's collider. */
        inline constexpr std::uint32_t Kinematic = 0x4DA6FFFFu;
        /** @brief A dynamic body's collider. */
        inline constexpr std::uint32_t Dynamic = 0x4DE64DFFu;
        /** @brief A trigger's collider, whatever body it is on. */
        inline constexpr std::uint32_t Trigger = 0xFF66E6FFu;
        /** @brief A contact's point and normal. */
        inline constexpr std::uint32_t Contact = 0xFF3333FFu;
        /** @brief A body's velocity. */
        inline constexpr std::uint32_t Velocity = 0xFFE633FFu;
        /** @brief A body's bounding box. */
        inline constexpr std::uint32_t Bounds = 0xFF9933FFu;
        /** @brief A joint's frameA: its X axis (red), Y axis (green) and Z axis (blue). */
        inline constexpr std::uint32_t JointAX = 0xE6194BFFu;
        inline constexpr std::uint32_t JointAY = 0x3CB44BFFu;
        inline constexpr std::uint32_t JointAZ = 0x4363D8FFu;
        /** @brief A joint's frameB: its X axis (pink), Y axis (yellow) and Z axis (cyan). */
        inline constexpr std::uint32_t JointBX = 0xFF99CCFFu;
        inline constexpr std::uint32_t JointBY = 0xFFFF4DFFu;
        inline constexpr std::uint32_t JointBZ = 0x4DFFFFFFu;
        /** @brief Every axis of an inactive joint's frames, drawn only on its bodies that are still bodies. */
        inline constexpr std::uint32_t InactiveJoint = 0x6B6B6BFFu;

        /** @brief A sleeping body's collider colour, or a disabled joint's axis colour: `colour` at half brightness,
         * alpha unchanged. */
        [[nodiscard]] constexpr std::uint32_t Dimmed(std::uint32_t colour)
        {
            return ((colour >> 1u) & 0x7F7F7F00u) | (colour & 0xFFu);
        }
    } // namespace DebugColour
} // namespace Fenrir
