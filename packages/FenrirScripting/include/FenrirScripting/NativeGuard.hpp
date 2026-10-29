#pragma once

// The native guard: what a native function checks a call against before acting, made from its call context and its
// extension's warn-once record (ADR-0045 for what it need not check: `in` and `out` pointers, and outputs' defaults).

#include <cstdint>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>

#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirScripting/WarnOnceRecord.hpp"

namespace Fenrir
{
    namespace Detail
    {
        template <typename T>
        inline constexpr bool IsOptional = false;
        template <typename T>
        inline constexpr bool IsOptional<std::optional<T>> = true;

        // What Require passes through: a pointer or an optional, which is empty when its check failed.
        template <typename T>
        concept Requirable = std::is_pointer_v<T> || IsOptional<T>;
    } // namespace Detail

    /**
     * @brief What a native function checks a call against before acting: the entity-alive rule, a requirement check
     * and a warning, each naming the native function (from the call context) and warning once per place and message
     * through the extension's record (debug builds only, as WarnOnceRecord does). A value made at the top of a native
     * function from its context and its extension state's `warnings`:
     *
     * ```cpp
     * NativeGuard guard(context, physics.warnings);
     * PhysicsWorld* world = guard.Require(PhysicsWorld::Of(scene), "the active scene has no physics world yet, so it
     * does nothing");
     * if (!guard.Alive(body)) return false;
     * ```
     *
     * A package's own lookups (its world, its store) are built on it, in its extension state.
     */
    class NativeGuard
    {
      public:
        NativeGuard(const CallContext& context, WarnOnceRecord& warnings) noexcept
            : m_context(&context), m_warnings(&warnings)
        {
        }

        /** @brief What Alive warns for an entity that is not alive in the active scene. */
        static constexpr std::string_view NotAlive = "the entity is not alive, so it does nothing";

        /**
         * @brief The entity's value as an entity of `entities`, the owner rule every native resolves an entity it is
         * given by: its value when `entities` owns it, or when it has no owner (a test names an entity by its id, and
         * a zeroed field names entity 0 with no scene); Entity::Null, which no list holds, when another scene's list
         * owns it, so a stale link from another scene is never alive here.
         */
        [[nodiscard]] static std::uint32_t ValueOf(const Entity& entity, EntityList& entities)
        {
            const std::uint32_t value = entity.GetId();
            return !entity.HasEntityList() || entity == entities.HandleOf(value) ? value : Entity::Null;
        }

        /**
         * @brief The entity-alive rule: `entity` as the active scene's handle if it is alive there (ValueOf), or
         * nothing, warning once for the caller's call site that the call does nothing.
         */
        [[nodiscard]] std::optional<Entity> Alive(const Entity& entity,
                                                  std::source_location where = std::source_location::current()) const
        {
            EntityList& entities = m_context->Entities();
            const std::uint32_t value = ValueOf(entity, entities);
            if (entities.HasEntity(value))
            {
                return entities.GetEntity(value);
            }
            m_warnings->Warn(*m_context, std::string(NotAlive), where);
            return std::nullopt;
        }

        /**
         * @brief `value` as it is, a pointer or an optional, warning `message` once for the caller's call site when it
         * is null or empty: `guard.Require(PhysicsWorld::Of(scene), "the active scene has no physics world yet, so it
         * does nothing")`.
         */
        template <Detail::Requirable T>
        [[nodiscard]] T Require(T value, const std::string& message,
                                std::source_location where = std::source_location::current()) const
        {
            if (!value)
            {
                m_warnings->Warn(*m_context, message, where);
            }
            return value;
        }

        /** @brief Warns `message` once for the caller's call site, under the native function's name. */
        void Warn(const std::string& message, std::source_location where = std::source_location::current()) const
        {
            m_warnings->Warn(*m_context, message, where);
        }

        /** @brief The call context it checks against. */
        [[nodiscard]] const CallContext& Context() const noexcept
        {
            return *m_context;
        }

      private:
        const CallContext* m_context;
        WarnOnceRecord* m_warnings;
    };
} // namespace Fenrir
