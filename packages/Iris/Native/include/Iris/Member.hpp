#pragma once

#include "CallResult.hpp"
#include "Method.hpp"
#include "Object.hpp"

#include <concepts>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Iris
{
    class Type;

    /**
     * @brief The value types a field or property can have: a PrimitiveValue, `std::optional<Object>` for a
     * class- or interface-typed member, or `std::optional<std::string>` for a string; an empty optional is
     * null (distinct from an empty string).
     */
    template <typename T>
    concept MemberValue = PrimitiveValue<T> || StructValue<T> || std::same_as<T, std::optional<Object>> ||
                          std::same_as<T, std::optional<std::string>>;

    namespace Detail
    {
        // An untyped member handle. Use Member<T> instead.
        class RawMember
        {
          public:
            // `target` is the Object for an instance member, or null for a static one.
            [[nodiscard]] CallResult<std::uint64_t> Get(const Object* target) const;
            // `value` is the primitive's slot; `objectValue` is the Object to store for an object member
            // (null stores null). The object's handle is resolved and checked here.
            [[nodiscard]] CallResult<void> Set(const Object* target, std::uint64_t value,
                                               const Object* objectValue) const;
            // Takes ownership of an object handle the bridge returned from this member's component.
            [[nodiscard]] Object AdoptObject(std::uint64_t handle) const;
            // Copies and releases a string block the bridge returned from this member's component.
            [[nodiscard]] std::optional<std::string> TakeString(std::uint64_t block) const;
            // Copies and releases a struct block the bridge returned from this member's component.
            void TakeStruct(std::uint64_t block, void* out, std::size_t size) const noexcept;

          private:
            friend class ::Iris::Type;
            struct State;
            // The target's handle (zero for a static member), or why the target cannot be used.
            [[nodiscard]] static std::optional<std::pair<CallStatus, std::string>> TargetHandle(std::uint64_t component,
                                                                                                const Object* target,
                                                                                                std::uint64_t& handle);
            explicit RawMember(std::shared_ptr<const State> state) noexcept;
            std::shared_ptr<const State> state;
        };
    } // namespace Detail

    /**
     * @brief A handle to a public field or property of type `T`, static or instance.
     *
     * Found with Type::FindField or Type::FindProperty. A static member is used without an object,
     * `Get()` and `Set(value)`; an instance member with one, `Get(object)` and `Set(object, value)`.
     * Using the wrong form, writing a read-only member (a readonly field, a constant, a property with
     * no setter) or reading a write-only property is InvalidArgument with a diagnostic. A property
     * runs its accessors, so an exception from one is ManagedException and leaves the object usable.
     *
     * For an object-typed member, `T` is `std::optional<Object>`: `Get` returns a new, independently
     * owned handle (empty for null), and `Set` takes a `const Object*` where `nullptr` stores null; the
     * object must be an instance of the member's type. Like Type, a Member is a cheap, copyable value
     * that holds no managed reference and stops working (InvalidHandle) when its component is released.
     */
    template <MemberValue T>
    class Member
    {
      public:
        // What Set takes: the value itself, or a pointer to the Object for an object member.
        using Value = std::conditional_t<
            std::same_as<T, std::optional<Object>>, const Object*,
            std::conditional_t<std::same_as<T, std::optional<std::string>>, std::optional<std::string_view>,
                               std::conditional_t<StructValue<T>, const T&, T>>>;

        [[nodiscard]] CallResult<T> Get() const
        {
            return Detail::ToTyped<T>(member, member.Get(nullptr));
        }
        [[nodiscard]] CallResult<T> Get(const Object& target) const
        {
            return Detail::ToTyped<T>(member, member.Get(&target));
        }
        [[nodiscard]] CallResult<void> Set(Value value) const
        {
            Detail::StringArgumentView text;
            return member.Set(nullptr, SlotOf(value, text), ObjectOf(value));
        }
        [[nodiscard]] CallResult<void> Set(const Object& target, Value value) const
        {
            Detail::StringArgumentView text;
            return member.Set(&target, SlotOf(value, text), ObjectOf(value));
        }

      private:
        friend class Type;
        explicit Member(Detail::RawMember rawMember) noexcept : member(std::move(rawMember))
        {
        }

        // A string member's slot points at `text`, which must outlive the call.
        static std::uint64_t SlotOf(Value value, Detail::StringArgumentView& text) noexcept
        {
            if constexpr (std::same_as<T, std::optional<Object>>)
                return 0;
            else if constexpr (std::same_as<T, std::optional<std::string>>)
            {
                text = Detail::MakeStringView(value);
                return reinterpret_cast<std::uintptr_t>(&text);
            }
            else if constexpr (StructValue<T>)
                return reinterpret_cast<std::uintptr_t>(&value);
            else
                return Detail::ToSlot<T>(value);
        }
        static const Object* ObjectOf(Value value) noexcept
        {
            if constexpr (std::same_as<T, std::optional<Object>>)
                return value;
            else
                return nullptr;
        }

        Detail::RawMember member;
    };
} // namespace Iris
