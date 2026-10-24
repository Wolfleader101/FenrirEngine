#pragma once

#include <cstdint>
#include <mutex>
#include <set>
#include <source_location>
#include <string>
#include <string_view>
#include <tuple>

#include "FenrirApp/App.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"

namespace Fenrir
{
    /**
     * @brief The physics extension's state, which every physics native function is given second: what they share, made
     * by PhysicsScriptingExtension and kept by the Scripting given the extension. In a header of its own because the
     * generated registration (Generated/FenrirPhysicsRegistration.cpp) declares the natives, which name it.
     */
    struct PhysicsScripting
    {
        /**
         * @brief The name of the native function `where` is in: the last name before its parameter list, whichever
         * spelling the compiler gives a function (with or without its signature).
         */
        [[nodiscard]] static std::string_view NativeName(const std::source_location& where)
        {
            std::string_view name = where.function_name();
            name = name.substr(0, name.find('('));
            const std::size_t last = name.find_last_of(": ");
            return last == std::string_view::npos ? name : name.substr(last + 1);
        }

        /**
         * @brief Logs "<native>: <message>" once for each place in the extension that notices a call doing nothing, and
         * each message, in debug builds only. <native> is the native function `where` is in.
         */
        void WarnOnce(const CallContext& context, const std::string& message,
                      std::source_location where = std::source_location::current())
        {
#ifndef NDEBUG
            // Keyed by the message too: one call site can notice more than one reason (no world, then no body).
            {
                const std::lock_guard<std::mutex> lock(mutex);
                if (!warned.emplace(where.file_name(), where.line(), where.column(), message).second)
                {
                    return;
                }
            }
            context.GetApp().Logger()->Warn("[physics scripting] {}: {} (warned once)", NativeName(where), message);
#else
            (void)context;
            (void)message;
            (void)where;
#endif
        }

        /** @brief The active scene's physics world, or null (warning once for the caller's call site). */
        PhysicsWorld* WorldOf(const CallContext& context, std::source_location where = std::source_location::current())
        {
            PhysicsWorld* world = PhysicsWorld::Of(context.GetApp().GetActiveScene());
            if (world == nullptr)
            {
                WarnOnce(context, "the active scene has no physics world yet, so it does nothing", where);
            }
            return world;
        }

        /**
         * @brief Whether `entity` is alive with a body (a RigidBody, or a Collider standing for its body, a character's
         * included); warns once if not.
         */
        bool HasBody(const CallContext& context, std::uint32_t entity,
                     std::source_location where = std::source_location::current())
        {
            EntityList& entities = context.Entities();
            if (entities.HasEntity(entity))
            {
                const Entity body = entities.GetEntity(entity);
                if (body.HasComponent<RigidBody>() || body.HasComponent<Collider>())
                {
                    return true;
                }
            }
            WarnOnce(context, "the entity has no body, so it does nothing", where);
            return false;
        }

        /** @brief The calls warned about, by where and why. Natives may run on several threads at once. */
        std::mutex mutex;
        /** @brief See mutex. */
        std::set<std::tuple<std::string_view, std::uint_least32_t, std::uint_least32_t, std::string>> warned;
    };
} // namespace Fenrir
