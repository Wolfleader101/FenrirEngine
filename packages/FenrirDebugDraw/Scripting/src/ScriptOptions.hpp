#pragma once

// ShapeOptions and TextOptions as C# passes them (Fenrir.DebugDraw's NativeShapeOptions and NativeTextOptions): without
// the group, which crosses as text of its own. The natives convert these to FenrirDebugDraw's own. This header is the
// binding's own API description (Scripting/api/FenrirDebugDrawScripting.api.toml), so their C# mirrors and mappings are
// generated from it, and they are checked against C# as scripts load. The doc comments are C#'s.

#include <cstdint>

#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirECS/Entity.hpp"

namespace Fenrir::ScriptOptions
{
    /** @brief A ShapeOptions without its group, as a native takes it. */
    struct ShapeOptions
    {
        /** @brief How long the item is kept, as ShapeOptions.Duration. */
        float duration = 0.0f;
        /** @brief The entity the positions are relative to, or none. */
        Entity relativeTo = Entity();
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
        /** @brief The entity the item is relative to, or none. */
        Entity relativeTo = Entity();
        /** @brief The height of the text in pixels. */
        float size = 16.0f;
    };
} // namespace Fenrir::ScriptOptions
