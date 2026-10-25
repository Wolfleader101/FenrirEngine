#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <set>
#include <source_location>
#include <string>
#include <string_view>
#include <tuple>

#include "FenrirApp/App.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
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
         * @brief Where a call draws: the active scene's DebugDraw, and the relativeTo entity C# named (a null Entity
         * for none).
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
         * @brief Logs a warning once for each place in the extension that notices a call doing nothing, and each
         * message, in debug builds only.
         */
        void WarnOnce(const CallContext& context, const std::string& message, std::source_location where)
        {
#ifndef NDEBUG
            {
                const std::lock_guard<std::mutex> lock(mutex);
                if (!warned.emplace(where.file_name(), where.line(), where.column(), message).second)
                {
                    return;
                }
            }
            context.GetApp().Logger()->Warn("[debug draw scripting] {} (warned once)", message);
#else
            (void)context;
            (void)message;
            (void)where;
#endif
        }

        /**
         * @brief The active scene's DebugDraw and the entity `relativeTo` (Entity::Null for none), unless the scene has
         * no DebugDraw or the entity is not alive: then nothing is drawn, and the caller's call site warns once, naming
         * `function`, the C# call.
         */
        std::optional<Target> TargetFor(
            const CallContext& context, std::string_view function,
            ScriptOptions::EntityValue relativeTo = ScriptOptions::EntityValue(Entity::Null),
            std::source_location where = std::source_location::current())
        {
            DebugDraw* draw = context.GetApp().GetActiveScene().GetResource<DebugDraw>();
            if (draw == nullptr)
            {
                WarnOnce(context,
                         std::string(function) + ": the active scene has no DebugDraw (FenrirDebugDraw is not "
                                                 "installed), so it draws nothing",
                         where);
                return std::nullopt;
            }
            if (relativeTo.value == Entity::Null)
            {
                return Target{draw, Entity()};
            }
            EntityList& entities = context.Entities();
            if (!entities.HasEntity(relativeTo.value))
            {
                WarnOnce(context, std::string(function) + ": the relativeTo entity is not alive, so it draws nothing",
                         where);
                return std::nullopt;
            }
            return Target{draw, entities.GetEntity(relativeTo.value)};
        }

        /** @brief The calls warned about, by where and why. Natives may run on several threads at once. */
        std::mutex mutex;
        /** @brief See mutex. */
        std::set<std::tuple<std::string_view, std::uint_least32_t, std::uint_least32_t, std::string>> warned;
    };
} // namespace Fenrir
