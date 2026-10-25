#pragma once

// ShapeOptions and TextOptions as C# passes them (Fenrir.DebugDraw's NativeShapeOptions and NativeTextOptions): without
// the group, which crosses as text of its own, and with the relativeTo entity as its 32-bit value, a C# Entity, since a
// Fenrir::Entity also holds a pointer to its list. The natives convert these to FenrirDebugDraw's own. This header is
// the binding's own API description (Scripting/api/FenrirDebugDrawScripting.api.toml), so their C# mirrors and mappings
// are generated from it, and they are checked against C# as scripts load. The doc comments are C#'s.

#include <cstdint>

#include "FenrirDebugDraw/DebugDraw.hpp"

namespace Fenrir::ScriptOptions
{
    /** @brief A C# Entity: an entity's 32-bit value alone. */
    struct EntityValue
    {
        EntityValue() = default;

        // Named after the component it sets (the leading underscore is ignored), as an API description needs.
        constexpr explicit EntityValue(std::uint32_t _value) : value(_value)
        {
        }

        std::uint32_t value = 0;
    };

    /** @brief A ShapeOptions without its group, as a native takes it. */
    struct ShapeOptions
    {
        /** @brief How long the item is kept, as ShapeOptions.Duration. */
        float duration = 0.0f;
        /** @brief The entity the positions are relative to, or Entity.None (Entity::Null). */
        EntityValue relativeTo = EntityValue(0xFFFFFFFFu);
        /** @brief Line thickness in pixels. */
        float thickness = 1.0f;
        /** @brief Whether the shape is depth tested or drawn on top. */
        DepthMode depth = DepthMode::Tested;
        /** @brief How many straight segments make a full circle of a round shape. */
        std::int32_t segments = 24;
    };

    /** @brief A TextOptions without its group, as a native takes it. */
    struct TextOptions
    {
        /** @brief How long the item is kept, as TextOptions.Duration. */
        float duration = 0.0f;
        /** @brief The entity the item is relative to, or Entity.None (Entity::Null). */
        EntityValue relativeTo = EntityValue(0xFFFFFFFFu);
        /** @brief The height of the text in pixels. */
        float size = 16.0f;
    };
} // namespace Fenrir::ScriptOptions
