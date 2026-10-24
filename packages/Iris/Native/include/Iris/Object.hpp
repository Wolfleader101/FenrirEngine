#pragma once

#include "CallResult.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace Iris
{
    // Not API: what the public templates need to talk to the bridge, including its wire format. It can change in
    // any release, so always build against the headers of the library you link.
    namespace Detail
    {
        class RawMethod;
        class RawMember;
        class RawArray;
        class CallOrigin;
        class ComponentCall;
        struct BridgeFunctions;
    } // namespace Detail

    /**
     * @brief Owns one managed object: created with Type::CreateObject, or returned from a method or member as
     * `std::optional<Object>`.
     *
     * While an Object exists, the bridge keeps the managed object alive (it survives GC) and its
     * component cannot be released: Component::Release returns InUse until every Object is released.
     * Release (or destruction) unroots the object. Moving transfers ownership.
     *
     * Destroying a Component force-releases it and drops any objects it still holds; their calls then
     * return InvalidHandle and releasing them returns InvalidHandle. An Object may outlive the Host and
     * the Component object, and is destroyed safely in any order. Like Component, it calls into the
     * runtime when destroyed, so it must not outlive `main`. Calls on one Object must be externally
     * serialized.
     */
    class Object
    {
      public:
        ~Object() noexcept;
        Object(Object&&) noexcept;
        Object& operator=(Object&&) noexcept;
        Object(const Object&) = delete;
        Object& operator=(const Object&) = delete;

        // True while this owner holds an object: false after Release and when moved from.
        [[nodiscard]] bool IsValid() const noexcept;

        // Idempotent: releasing an already released or moved-from owner succeeds. Invalidates this owner
        // either way; the status is InvalidHandle if the object had already been dropped because its
        // component was released first.
        [[nodiscard]] CallResult<void> Release();

      private:
        friend class Type;
        friend class Detail::RawMethod;
        friend class Detail::RawMember;
        friend class Detail::RawArray;
        friend class Detail::CallOrigin;
        friend class Detail::ComponentCall;
        struct State;
        explicit Object(std::unique_ptr<State> state) noexcept;
        std::unique_ptr<State> state;
    };

    namespace Detail
    {
        /**
         * @brief Where the results of a call on one component's handle go: an object handle the bridge returned becomes
         * an owner, and a string or struct block it returned is copied and given back.
         *
         * Every raw handle (method, member, array) gives one, so the public templates take results the same way
         * whichever handle made the call. It refers to the handle's bridge for the length of the call, so it is never
         * stored.
         */
        class CallOrigin
        {
          public:
            CallOrigin(const std::shared_ptr<const BridgeFunctions>& bridge, std::uint64_t component) noexcept
                : bridge(bridge), component(component)
            {
            }

            /** @brief Takes ownership of an object handle the bridge returned. */
            [[nodiscard]] Object Adopt(std::uint64_t handle) const;
            /** @brief Copies and releases a string block the bridge returned; zero is null. */
            [[nodiscard]] std::optional<std::string> TakeString(std::uint64_t block) const;
            /** @brief Copies and releases a struct block the bridge returned (zero fills `out` with zeros). */
            void TakeStruct(std::uint64_t block, void* out, std::size_t size) const noexcept;

          private:
            const std::shared_ptr<const BridgeFunctions>& bridge;
            std::uint64_t component;
        };
    } // namespace Detail
} // namespace Iris
