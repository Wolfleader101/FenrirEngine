#pragma once

#include "ManagedCallResult.hpp"

#include <memory>

namespace Fenrir::Scripting
{
    struct ComponentInfo
    {
        // Snapshot owned by native code, independent of the component lifetime.
        std::string assemblyName;
        bool isCollectible = false;
    };

    struct ComponentInfoResult
    {
        ManagedCallStatus status = ManagedCallStatus::NotInitialized;
        std::optional<ComponentInfo> value;
        std::string diagnostic;
        bool diagnosticTruncated = false;
    };

    struct ComponentReleaseResult
    {
        ManagedCallStatus status = ManagedCallStatus::NotInitialized;
        std::string diagnostic;
        bool diagnosticTruncated = false;
    };

    // Owns a registration in the resident managed bridge. May outlive DotNetHost.
    // Destruction releases the registration and requests cooperative unloading;
    // it does not prove collection has completed or stop CoreCLR. Explicit Release
    // reports errors; destruction performs best-effort cleanup without throwing.
    // Externally serialize calls, moves, release and destruction on this owner.
    class ManagedComponent
    {
      public:
        ~ManagedComponent() noexcept;
        ManagedComponent(ManagedComponent&&) noexcept;
        ManagedComponent& operator=(ManagedComponent&&) noexcept;
        ManagedComponent(const ManagedComponent&) = delete;
        ManagedComponent& operator=(const ManagedComponent&) = delete;

        [[nodiscard]] bool IsLoaded() const noexcept;
        [[nodiscard]] ComponentInfoResult GetInfo() const;
        // Idempotent. Invalidates this owner even if requesting unload reports an
        // error; managed registration removal precedes the unload request.
        [[nodiscard]] ComponentReleaseResult Release();

      private:
        friend class DotNetHost;
        struct State;
        explicit ManagedComponent(std::unique_ptr<State> state) noexcept;
        std::unique_ptr<State> state;
    };

    struct ComponentLoadResult
    {
        ManagedCallStatus status = ManagedCallStatus::NotInitialized;
        // Present only on success. No managed token/address is public.
        std::unique_ptr<ManagedComponent> value;
        std::string diagnostic;
        bool diagnosticTruncated = false;
    };
} // namespace Fenrir::Scripting
