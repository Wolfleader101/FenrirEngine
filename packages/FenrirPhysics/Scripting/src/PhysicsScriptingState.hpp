#pragma once

#include <optional>

#include "FenrirECS/Entity.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirScripting/EntityValue.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirScripting/WarnOnceRecord.hpp"

namespace Fenrir
{
    /**
     * @brief The physics extension's state, which every physics native function is given second: what they share, made
     * by Physics::ScriptingExtension and kept by the Scripting given the extension. In a header of its own because
     * the generated natives header (Generated/FenrirPhysicsNatives.hpp) declares the natives, which name it.
     *
     * Its lookups are built on the native guard (NativeGuard.hpp): each warns once per native function and message,
     * under the native's name, which the call context carries, so no native spells its own.
     */
    struct PhysicsScripting
    {
        /** @brief The active scene's physics world, or null (warning once that the native does nothing). */
        PhysicsWorld* WorldOf(const CallContext& context)
        {
            const NativeGuard guard(context, warnings);
            return guard.Require(PhysicsWorld::Of(context.GetApp().GetActiveScene()),
                                 "the active scene has no physics world yet, so it does nothing");
        }

        /**
         * @brief Whether `entity` is alive with a body (a RigidBody, or a Collider standing for its body, a character's
         * included); warns once if not: through the guard's entity-alive rule when it is not alive, and that it has no
         * body when it is.
         */
        bool HasBody(const CallContext& context, EntityValue entity)
        {
            const NativeGuard guard(context, warnings);
            const std::optional<Entity> body = guard.Alive(entity);
            if (!body)
            {
                return false;
            }
            if (body->HasComponent<RigidBody>() || body->HasComponent<Collider>())
            {
                return true;
            }
            guard.Warn("the entity has no body, so it does nothing");
            return false;
        }

        /** @brief What the natives have warned about, so that each place warns once. */
        WarnOnceRecord warnings{"physics scripting"};
    };
} // namespace Fenrir
