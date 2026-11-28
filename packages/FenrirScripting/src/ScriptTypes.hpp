#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <Iris/Iris.hpp>

#include "FenrirECS/ComponentType.hpp"
#include "FenrirTypes/TypeRegistry.hpp"

#include "ScriptComponents.hpp"

namespace Fenrir::Detail
{
    /**
     * @brief The type descriptions of one script generation: a fixed-layout description of each component its script
     * report holds, and a nested one of each struct those components hold, made from the report at run time and owned
     * here for as long as they are registered (ADR-0074, ADR-0083).
     *
     * Each description's default is the type's `new T()`, whose bytes the report carries. A description's `construct`
     * takes no context, so each one copies its type's defaults from a process-wide slot this arena holds while it
     * lives; there are `SlotCount` slots, so a process describes at most that many script types at once.
     *
     * Building checks everything and registers nothing; registering is all or nothing; unregistering goes holders
     * first.
     */
    class ScriptTypes
    {
      public:
        /** @brief How many script types the process can describe at once. */
        static constexpr std::size_t SlotCount = 1024;

        /** @brief The ComponentType registered for the script component of this name; empty when there is none. */
        using ComponentTypeOf = std::function<std::optional<ComponentType>(std::string_view name)>;

        ScriptTypes();
        ~ScriptTypes();
        ScriptTypes(const ScriptTypes&) = delete;
        ScriptTypes& operator=(const ScriptTypes&) = delete;

        /**
         * @brief Makes a description of every struct and component in `report`, checking each against `types`: no name
         * a registered type holds, every field inside its type and the size its kind takes, every struct a field holds
         * described by the report or registered, and the defaults the type's size. Registers nothing.
         *
         * @return success, or a diagnostic naming the type and the field
         */
        [[nodiscard]] Iris::CallResult<void> Build(const ScriptReport& report, const TypeRegistry& types);

        /**
         * @brief Registers every description `Build` made with `types`, the structs before the components that hold
         * them, and attaches each component's ComponentType (FenrirECS's ComponentTypeAttachment), so the scene reader
         * and writer reach it by name. All or none: a refusal unregisters what this call registered.
         *
         * @return success, or a diagnostic naming the type the registry refused
         */
        [[nodiscard]] Iris::CallResult<void> Register(TypeRegistry& types, const ComponentTypeOf& componentTypeOf);

        /**
         * @brief Unregisters what `Register` registered, holders first.
         *
         * @return Whether every description went: when one stays (the registry refused it, and a later call tries it
         * again), the descriptions must outlive the registry, so this must not be destroyed.
         */
        [[nodiscard]] bool Unregister(TypeRegistry& types);

        /** @brief Whether any description is registered. */
        [[nodiscard]] bool IsRegistered() const noexcept;

      private:
        struct Entry;

        // The structs, each after the structs it holds, then the components: the order they register in.
        std::vector<std::unique_ptr<Entry>> m_entries;
    };
} // namespace Fenrir::Detail
