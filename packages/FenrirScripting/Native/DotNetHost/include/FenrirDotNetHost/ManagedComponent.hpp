#pragma once

#include "ManagedCallResult.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace Fenrir::Scripting
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
     * @brief Owns one collectible component loaded by DotNetHost::LoadComponent.
     *
     * The owner may outlive the DotNetHost that loaded it. Release (or destruction) removes the
     * bridge's strong references and requests cooperative unloading; collection happens later and is
     * observed with GetUnloadStatus. Neither forces GC or stops CoreCLR.
     *
     * Destruction calls into the runtime, so owners must be destroyed before `main` returns, never
     * during static destruction. Calls, moves, release and destruction on one owner must be
     * externally serialized. A moved-from owner returns InvalidHandle.
     */
    class ManagedComponent
    {
      public:
        ~ManagedComponent() noexcept;
        ManagedComponent(ManagedComponent&&) noexcept;
        ManagedComponent& operator=(ManagedComponent&&) noexcept;
        ManagedComponent(const ManagedComponent&) = delete;
        ManagedComponent& operator=(const ManagedComponent&) = delete;

        // False after Release and when moved from.
        [[nodiscard]] bool IsLoaded() const noexcept;

        [[nodiscard]] ManagedCallResult<ComponentInfo> GetInfo() const;

        // NotRequested while loaded; Pending or Collected after Release (a snapshot, with no
        // deadline for Pending). Remains available after Release and host destruction.
        [[nodiscard]] ManagedCallResult<ComponentUnloadState> GetUnloadStatus() const;

        // Calls a public, non-generic, static `int Method(int, int)` declared on a public closed type
        // in this component. Names are case-sensitive UTF-8 without null characters, borrowed for
        // the call. Exceptions thrown by the lookup or the method return ManagedException
        // (OverflowException returns Overflow) with their details.
        [[nodiscard]] ManagedCallResult<std::int32_t> InvokeInt32(std::string_view typeName,
                                                                  std::string_view methodName, std::int32_t left,
                                                                  std::int32_t right) const;

        // Idempotent. Invalidates this owner even when the unload request reports an error.
        [[nodiscard]] ManagedCallResult<void> Release();

      private:
        friend class DotNetHost;
        struct State;
        explicit ManagedComponent(std::unique_ptr<State> state) noexcept;
        std::unique_ptr<State> state;
    };
} // namespace Fenrir::Scripting
