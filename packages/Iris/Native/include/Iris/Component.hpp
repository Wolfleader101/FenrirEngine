#pragma once

#include "CallResult.hpp"
#include "Type.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Iris
{
    // Not API: what the public templates need to talk to the bridge, including its wire format. It can change in
    // any release, so always build against the headers of the library you link.
    namespace Detail
    {
        class RawArray;
        struct TestAccess;
    } // namespace Detail

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
     * observed with GetUnloadState. Neither forces GC or stops CoreCLR.
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
        [[nodiscard]] CallResult<ComponentUnloadState> GetUnloadState() const;

        /**
         * @brief Waits up to `timeout` for a released component to be collected, and reports Collected, or
         * Pending if the time ran out (the component stays unloading; nothing is lost and the host stays
         * usable). Iris never forces a collection by itself, so this is the one place it happens: while the
         * component is Pending, each attempt runs a full, blocking garbage collection with finalizers on
         * the runtime, then looks again. That is expensive, so call it only at a reload point, not every frame.
         *
         * A loaded component returns NotRequested at once, and an already collected one Collected at once,
         * without collecting. A Pending result after a long wait usually means a root still holds the
         * component: a running thread, a static that holds one of its types, or an object handed to a host
         * that kept it. A negative timeout is InvalidArgument and a moved-from owner is InvalidHandle. Works
         * after Release, like GetUnloadState, and after the Host is gone.
         */
        [[nodiscard]] CallResult<ComponentUnloadState> WaitForUnload(std::chrono::milliseconds timeout) const;

        // Lists the component's public, non-generic types as handles, in declaration order (the order they
        // appear in the assembly's metadata), nested public types included. These are exactly the types
        // FindType finds, except that a type that fails to load (a base class in a missing assembly) is left out and
        // reported to the log sink as a warning; internal and generic-definition types are not listed. A snapshot:
        // types are not added or removed after load, and the handles behave like those from FindType.
        [[nodiscard]] CallResult<std::vector<Type>> GetTypes() const;

        // Finds a public, closed type defined in this component's assembly by full name (for example
        // "My.Namespace.Calculator"; nested types use '+'). The name is case-sensitive UTF-8 without
        // null characters, borrowed for the call. Looking the same type up again returns an equivalent
        // handle. A missing, non-public or generic-definition type is NotFound; types from the
        // component's dependencies are not found through it.
        [[nodiscard]] CallResult<Type> FindType(std::string_view fullName) const;

        /**
         * @brief Validates a StructMapping against the managed type in this component without calling any
         * method. Name, size, field count and each field's name, offset and kind are checked, nested structs,
         * enums and fixed-size arrays included, with the same rules and diagnostics as the first use of the
         * mapping as an argument, return or member. Success registers the mapping exactly as that first use
         * does, so validating again (or using the struct afterwards) is harmless.
         *
         * Call it for every mapping when a component loads if native code shares struct memory with managed
         * code by pointer (an internal call receives structs by pointer and never checks their layout).
         * A missing type is NotFound; a mismatch or a type that is not blittable is InvalidArgument with a
         * diagnostic naming the field; a released component is InvalidHandle. A failure leaves the component
         * usable.
         */
        template <StructValue T>
        [[nodiscard]] CallResult<void> ValidateStruct() const
        {
            return ValidateStructRaw(Detail::StructInfoOf<T>);
        }

        // Releases the component and requests cooperative unloading. Idempotent. Invalidates this owner even
        // when the unload request reports an error, except that a component with live Objects is refused
        // with InUse and stays loaded and usable: release the objects first. Destroying the owner
        // releases regardless, dropping any objects it still holds.
        [[nodiscard]] CallResult<void> Release();

      private:
        friend class Host;
        friend class Detail::RawArray;
        // Iris's own tests build a component over a bridge table of their own (Native/src/TestAccess.hpp, not
        // installed).
        friend struct Detail::TestAccess;
        struct State;
        explicit Component(std::unique_ptr<State> state) noexcept;
        [[nodiscard]] CallResult<void> ValidateStructRaw(const Detail::StructInfo& info) const;
        std::unique_ptr<State> state;
    };
} // namespace Iris
