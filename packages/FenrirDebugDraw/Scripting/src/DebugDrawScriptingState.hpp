#pragma once

#include <optional>
#include <string_view>

#include "FenrirApp/App.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirScripting/EntityValue.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirScripting/WarnOnceRecord.hpp"
#include "ScriptOptions.hpp"

namespace Fenrir
{
    /**
     * @brief The debug draw extension's state, which every debug draw native function is given second: what they share,
     * made by DebugDrawing::ScriptingExtension and kept by the Scripting given the extension. In a header of its own
     * because the generated registration (Generated/FenrirDebugDrawRegistration.cpp) declares the natives, which name
     * it.
     */
    struct DebugDrawScripting
    {
        /**
         * @brief Where a call draws: the active scene's DebugDraw, and the relativeTo entity C# named, as a handle of
         * that scene (a null Entity for none).
         */
        struct Target
        {
            DebugDraw* draw;
            Entity relativeTo;

            /** @brief FenrirDebugDraw's ShapeOptions for C#'s, in `group`. */
            [[nodiscard]] Fenrir::ShapeOptions Shape(const ScriptOptions::ShapeOptions& options,
                                                     std::string_view group) const
            {
                return {.group = group,
                        .duration = options.duration,
                        .relativeTo = relativeTo,
                        .thickness = options.thickness,
                        .depth = options.depth == DepthMode::OnTop ? DepthMode::OnTop : DepthMode::Tested,
                        .segments = options.segments};
            }

            /** @brief FenrirDebugDraw's TextOptions for C#'s, in `group`. */
            [[nodiscard]] Fenrir::TextOptions Text(const ScriptOptions::TextOptions& options,
                                                   std::string_view group) const
            {
                return {.group = group, .duration = options.duration, .relativeTo = relativeTo, .size = options.size};
            }
        };

        /**
         * @brief The active scene's DebugDraw and the entity `relativeTo` (a null Entity for none) resolved in that
         * scene by its value, as any entity a native is given, unless the scene has no DebugDraw or the entity is not
         * alive: then nothing is drawn, and the native guard warns once under the calling native function's name.
         */
        std::optional<Target> TargetFor(const CallContext& context, const Entity& relativeTo = Entity())
        {
            const NativeGuard guard(context, warnings);
            DebugDraw* draw = guard.Require(
                context.GetApp().GetActiveScene().GetResource<DebugDraw>(),
                "the active scene has no DebugDraw (FenrirDebugDraw is not installed), so it draws nothing");
            if (draw == nullptr)
            {
                return std::nullopt;
            }
            if (relativeTo.GetId() == Entity::Null)
            {
                return Target{draw, Entity()};
            }
            const std::optional<Entity> entity = guard.Alive(EntityValue(relativeTo.GetId()));
            if (!entity)
            {
                return std::nullopt;
            }
            return Target{draw, *entity};
        }

        /** @brief What the natives have warned about, so that each place warns once. */
        WarnOnceRecord warnings{"debug draw scripting"};
    };
} // namespace Fenrir
