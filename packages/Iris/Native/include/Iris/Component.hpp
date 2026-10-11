#pragma once

#include "CallResult.hpp"
#include "Type.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Iris
{
    namespace Detail
    {
        class RawArray;
    }

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

        // Lists the component's public, non-generic types as handles, in declaration order (the order they
        // appear in the assembly's metadata), nested public types included. These are exactly the types
        // FindType finds; internal and generic-definition types are not listed. A snapshot: types are not
        // added or removed after load, and the handles behave like those from FindType.
        [[nodiscard]] CallResult<std::vector<Type>> GetTypes() const;

        // Finds a public, closed type defined in this component's assembly by full name (for example
        // "My.Namespace.Calculator"; nested types use '+'). The name is case-sensitive UTF-8 without
        // null characters, borrowed for the call. Looking the same type up again returns an equivalent
        // handle. A missing, non-public or generic-definition type is NotFound; types from the
        // component's dependencies are not found through it.
        [[nodiscard]] CallResult<Type> FindType(std::string_view fullName) const;

        // Releases the component and requests cooperative unloading. Idempotent. Invalidates this owner even
        // when the unload request reports an error, except that a component with live Objects is refused
        // with InUse and stays loaded and usable: release the objects first. Destroying the owner
        // releases regardless, dropping any objects it still holds.
        [[nodiscard]] CallResult<void> Release();

      private:
        friend class Host;
        friend class Detail::RawArray;
        struct State;
        explicit Component(std::unique_ptr<State> state) noexcept;
        std::unique_ptr<State> state;
    };
} // namespace Iris
