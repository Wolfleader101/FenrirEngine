#pragma once

#include "CallResult.hpp"
#include "Type.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace Iris
{
    struct ComponentInfo
    {
        std::string assemblyName;
    };

    enum class ComponentUnloadState : std::int32_t
    {
        NotRequested = 0,
        Pending = 1,
        Collected = 2
    };

    /**
     * @brief Owns one collectible component loaded by Host::LoadComponent.
     *
     * The owner may outlive the Host that loaded it. Release (or destruction) removes the
     * bridge's strong references and requests cooperative unloading; collection happens later and is
     * observed with GetUnloadStatus. Neither forces GC or stops CoreCLR.
     *
     * Destruction calls into the runtime, so owners must be destroyed before `main` returns, never
     * during static destruction. Calls, moves, release and destruction on one owner must be
     * externally serialized. A moved-from owner returns InvalidHandle.
     */
    class Component
    {
      public:
        ~Component() noexcept;
        Component(Component&&) noexcept;
        Component& operator=(Component&&) noexcept;
        Component(const Component&) = delete;
        Component& operator=(const Component&) = delete;

        // False after Release and when moved from.
        [[nodiscard]] bool IsLoaded() const noexcept;

        [[nodiscard]] CallResult<ComponentInfo> GetInfo() const;

        // NotRequested while loaded; Pending or Collected after Release (a snapshot, with no
        // deadline for Pending). Remains available after Release and host destruction.
        [[nodiscard]] CallResult<ComponentUnloadState> GetUnloadStatus() const;

        // Finds a public, closed type defined in this component's assembly by full name (for example
        // "My.Namespace.Calculator"; nested types use '+'). The name is case-sensitive UTF-8 without
        // null characters, borrowed for the call. Looking the same type up again returns an equivalent
        // handle. A missing, non-public or generic-definition type is NotFound; types from the
        // component's dependencies are not found through it.
        [[nodiscard]] CallResult<Type> FindType(std::string_view fullName) const;

        // Calls a public, non-generic, static `int Method(int, int)` declared on a public closed type
        // in this component. Names are case-sensitive UTF-8 without null characters, borrowed for
        // the call. Exceptions thrown by the lookup or the method return ManagedException
        // (OverflowException returns Overflow) with their details.
        [[nodiscard]] CallResult<std::int32_t> InvokeInt32(std::string_view typeName, std::string_view methodName,
                                                           std::int32_t left, std::int32_t right) const;

        // Idempotent. Invalidates this owner even when the unload request reports an error.
        [[nodiscard]] CallResult<void> Release();

      private:
        friend class Host;
        struct State;
        explicit Component(std::unique_ptr<State> state) noexcept;
        std::unique_ptr<State> state;
    };
} // namespace Iris
