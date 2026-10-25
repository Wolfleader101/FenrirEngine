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
#include "FenrirECS/Entity.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirRenderer/Renderer.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"

namespace Fenrir
{
    namespace RendererScript
    {
        /** @brief An entity as the natives take and return it: its 32-bit value, which C# passes as an Entity. */
        using EntityId = std::uint32_t;
    } // namespace RendererScript

    /**
     * @brief The renderer extension's state, which every renderer native function is given second: the record of the
     * warnings it has logged, made by Renderer::ScriptingExtension and kept by the Scripting given the extension. In a
     * header of its own because the generated registration (Generated/FenrirRendererRegistration.cpp) declares the
     * natives, which name it.
     */
    struct RendererScripting
    {
        /**
         * @brief Logs `message` once for each place in the extension that notices a call doing nothing, and each
         * message, in debug builds only.
         */
        void WarnOnce(const CallContext& context, const std::string& message,
                      std::source_location where = std::source_location::current())
        {
#ifndef NDEBUG
            // Keyed by the message too: one call site can notice more than one reason.
            {
                const std::lock_guard<std::mutex> lock(mutex);
                if (!warned.emplace(where.file_name(), where.line(), where.column(), message).second)
                {
                    return;
                }
            }
            context.GetApp().Logger()->Warn("[renderer scripting] {} (warned once)", message);
#else
            (void)context;
            (void)message;
            (void)where;
#endif
        }

        /** @brief Whether the app has a renderer; warns once for the caller's call site if not. */
        bool HasRenderer(const CallContext& context, std::string_view function,
                         std::source_location where = std::source_location::current())
        {
            if (Renderer::IsInstalled(context.GetApp()))
            {
                return true;
            }
            WarnOnce(context,
                     std::string(function) + ": the app has no renderer (FenrirRenderer is not installed), so it does "
                                             "nothing",
                     where);
            return false;
        }

        /** @brief The entity `id` of the active scene if it is alive; warns once for the caller's call site if not. */
        std::optional<Entity> Alive(const CallContext& context, RendererScript::EntityId id, std::string_view function,
                                    std::source_location where = std::source_location::current())
        {
            EntityList& entities = context.Entities();
            if (entities.HasEntity(id))
            {
                return entities.GetEntity(id);
            }
            WarnOnce(context, std::string(function) + ": the entity is not alive, so it does nothing", where);
            return std::nullopt;
        }

        /** @brief The camera entity `id` if the app has a renderer and it is alive with a Camera; warns once if not. */
        std::optional<Entity> CameraFor(const CallContext& context, RendererScript::EntityId id,
                                        std::string_view function,
                                        std::source_location where = std::source_location::current())
        {
            if (!HasRenderer(context, function, where))
            {
                return std::nullopt;
            }
            std::optional<Entity> camera = Alive(context, id, function, where);
            if (camera && !camera->HasComponent<Camera>())
            {
                WarnOnce(context, std::string(function) + ": the entity has no Camera, so it does nothing", where);
                return std::nullopt;
            }
            return camera;
        }

      private:
        std::mutex mutex;
        std::set<std::tuple<std::string_view, std::uint_least32_t, std::uint_least32_t, std::string>> warned;
    };
} // namespace Fenrir
