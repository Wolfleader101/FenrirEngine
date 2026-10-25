#pragma once

#include <optional>
#include <source_location>
#include <string>
#include <string_view>

#include "FenrirApp/App.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirRenderer/Renderer.hpp"
#include "FenrirScripting/EntityValue.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirScripting/WarnOnceRecord.hpp"

namespace Fenrir
{
    /**
     * @brief The renderer extension's state, which every renderer native function is given second: the record of the
     * warnings it has logged, made by Renderer::ScriptingExtension and kept by the Scripting given the extension. In a
     * header of its own because the generated registration (Generated/FenrirRendererRegistration.cpp) declares the
     * natives, which name it.
     */
    struct RendererScripting
    {
        /** @brief Logs `message` once for each place in the extension that notices a call doing nothing (warnings). */
        void WarnOnce(const CallContext& context, const std::string& message,
                      std::source_location where = std::source_location::current())
        {
            warnings.Warn(context.GetApp(), message, where);
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
        std::optional<Entity> Alive(const CallContext& context, EntityValue id, std::string_view function,
                                    std::source_location where = std::source_location::current())
        {
            EntityList& entities = context.Entities();
            if (entities.HasEntity(id.value))
            {
                return entities.GetEntity(id.value);
            }
            WarnOnce(context, std::string(function) + ": the entity is not alive, so it does nothing", where);
            return std::nullopt;
        }

        /** @brief The camera entity `id` if the app has a renderer and it is alive with a Camera; warns once if not. */
        std::optional<Entity> CameraFor(const CallContext& context, EntityValue id, std::string_view function,
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
        WarnOnceRecord warnings{"renderer scripting"};
    };
} // namespace Fenrir
