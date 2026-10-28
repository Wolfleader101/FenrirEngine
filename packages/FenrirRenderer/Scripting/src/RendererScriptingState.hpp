#pragma once

#include <optional>

#include "FenrirApp/App.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirRenderer/Renderer.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirScripting/WarnOnceRecord.hpp"

namespace Fenrir
{
    /**
     * @brief The renderer extension's state, which every renderer native function is given second: the record of the
     * warnings it has logged, made by Renderer::ScriptingExtension and kept by the Scripting given the extension. In a
     * header of its own because the generated registration (Generated/FenrirRendererRegistration.cpp) declares the
     * natives, which name it.
     *
     * Its lookups are built on the native guard (NativeGuard.hpp): each warns once per native function and message,
     * under the native's name, which the call context carries, so no native spells its own.
     */
    struct RendererScripting
    {
        /** @brief Whether the app has a renderer; warns once that the native does nothing if not. */
        bool HasRenderer(const CallContext& context)
        {
            if (Renderer::IsInstalled(context.GetApp()))
            {
                return true;
            }
            const NativeGuard guard(context, warnings);
            guard.Warn("the app has no renderer (FenrirRenderer is not installed), so it does nothing");
            return false;
        }

        /**
         * @brief `entity` as the active scene's handle if the app has a renderer and it is alive there with a Camera;
         * warns once if not: through the guard's entity-alive rule when it is not alive, and that it has no Camera when
         * it is.
         */
        std::optional<Entity> CameraFor(const CallContext& context, const Entity& entity)
        {
            if (!HasRenderer(context))
            {
                return std::nullopt;
            }
            const NativeGuard guard(context, warnings);
            const std::optional<Entity> camera = guard.Alive(entity);
            if (camera && !camera->HasComponent<Camera>())
            {
                guard.Warn("the entity has no Camera, so it does nothing");
                return std::nullopt;
            }
            return camera;
        }

        /** @brief What the natives have warned about, so that each place warns once. */
        WarnOnceRecord warnings{"renderer scripting"};
    };
} // namespace Fenrir
