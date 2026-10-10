#pragma once

#include "ManagedCallResult.hpp"

#include <memory>
#include <string_view>

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

    enum class ComponentUnloadState : std::int32_t
    {
        NotRequested = 0,
        Pending = 1,
        Collected = 2
    };

    struct ComponentUnloadResult
    {
        ManagedCallStatus status = ManagedCallStatus::InvalidHandle;
        std::optional<ComponentUnloadState> value;
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
        // Observes a long weak reference without forcing GC or retrieving the target.
        // Remains usable after Release and host destruction. Pending has no deadline;
        // Collected is a snapshot that the released context was observed collected.
        // Destruction/move-assignment forgets the private weak tracking record,
        // without cancelling unloading. A moved-from owner returns InvalidHandle.
        [[nodiscard]] ComponentUnloadResult GetUnloadStatus() const;
        // Names are case-sensitive, explicit-length UTF-8 with no null characters.
        // Borrows them only during this call; each must be nonempty and <= INT32_MAX bytes.
        // Selects a public, non-generic static int(int, int), declared on a public closed
        // type in this assembly. UCO methods are excluded. Lookup/ordinary exceptions
        // produce ManagedException; overflow produces Overflow. Only success has a value.
        // Failures carry owning managed exception details, bounded to 4096 UTF-8 bytes
        // and cut on scalar boundaries; invalid details fall back to native text.
        // May outlive the host. A released/moved owner returns InvalidHandle first.
        [[nodiscard]] Int32CallResult InvokeInt32(std::string_view typeName, std::string_view methodName,
                                                  std::int32_t left, std::int32_t right) const;
        // Idempotent. Invalidates this owner even if requesting unload reports an
        // error; managed strong-reference removal precedes the unload request.
        // GetUnloadStatus remains available afterward; release itself does not force GC.
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
