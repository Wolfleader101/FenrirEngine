#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "FenrirECS/Entity.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
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
     * under the native's name, which the call context carries, so no native spells its own. The texts the natives
     * warn with are named here, one per rule, so the package's rules read in one place and its rules tests name
     * them (spec: rules harness, "The texts are named where the rules are").
     */
    struct PhysicsScripting
    {
        /** @brief Warned when the active scene has no physics world: the native does nothing. */
        static constexpr std::string_view NoWorld = "the active scene has no physics world yet, so it does nothing";

        /** @brief Warned when the entity is alive with neither a RigidBody nor a Collider: the native does nothing. */
        static constexpr std::string_view NoBody = "the entity has no body, so it does nothing";

        /** @brief Warned by GetJointState when the entity is not alive or has no Joint: the state reads zeroed. */
        static constexpr std::string_view NoJoint = "the entity has no Joint, so it reads zeroed";

        /**
         * @brief Warned by FrameFromWorld when the body `bodyName` names ("bodyA" or "bodyB") is not alive: the
         * frames read as identity.
         */
        [[nodiscard]] static std::string NotAliveFrames(std::string_view bodyName)
        {
            return std::string(bodyName) + " is not alive, so it gives identity frames";
        }

        /** @brief The active scene's physics world, or null (warning once that the native does nothing). */
        PhysicsWorld* WorldOf(const CallContext& context)
        {
            const NativeGuard guard(context, warnings);
            return guard.Require(PhysicsWorld::Of(context.GetApp().GetActiveScene()), std::string(NoWorld));
        }

        /**
         * @brief `entity` as the active scene's handle, if it is alive there with a body (a RigidBody, or a Collider
         * standing for its body, a character's included); nothing, warning once, if not: through the guard's
         * entity-alive rule when it is not alive, and that it has no body when it is.
         */
        std::optional<Entity> BodyOf(const CallContext& context, const Entity& entity)
        {
            const NativeGuard guard(context, warnings);
            const std::optional<Entity> body = guard.Alive(entity);
            if (!body)
            {
                return std::nullopt;
            }
            if (body->HasComponent<RigidBody>() || body->HasComponent<Collider>())
            {
                return body;
            }
            guard.Warn(std::string(NoBody));
            return std::nullopt;
        }

        /** @brief What the natives have warned about, so that each place warns once. */
        WarnOnceRecord warnings{"physics scripting"};
    };
} // namespace Fenrir
