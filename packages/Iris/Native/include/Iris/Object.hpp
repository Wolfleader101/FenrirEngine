#pragma once

#include "CallResult.hpp"

#include <memory>

namespace Iris
{
    // Not API: what the public templates need to talk to the bridge, including its wire format. It can change in
    // any release, so always build against the headers of the library you link.
    namespace Detail
    {
        class RawMethod;
        class RawMember;
        class RawArray;
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
        struct State;
        explicit Object(std::unique_ptr<State> state) noexcept;
        std::unique_ptr<State> state;
    };
} // namespace Iris
