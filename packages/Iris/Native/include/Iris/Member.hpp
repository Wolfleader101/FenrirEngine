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
     * @brief The value types a field or property can have: a PrimitiveValue, a mapped struct or enum,
     * `std::optional<Object>` for a class- or interface-typed member, or `std::optional<std::string>` for a
     * string; an empty optional is null (distinct from an empty string). Arrays are not member values.
     */
    template <typename T>
    concept MemberValue = PrimitiveValue<T> || StructValue<T> || EnumValue<T> ||
                          std::same_as<T, std::optional<Object>> || std::same_as<T, std::optional<std::string>>;

    // Not API: what the public templates need to talk to the bridge, including its wire format. It can change in
    // any release, so always build against the headers of the library you link.
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
            // Where this member's results go (see CallOrigin).
            [[nodiscard]] CallOrigin Origin() const noexcept;

          private:
            friend class ::Iris::Type;
            struct State;
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
     * object must be an instance of the member's type. A string member's `Set` takes
     * `std::optional<std::string_view>` (empty stores null) and a struct member's a `const T&` (see Value).
     * Like Type, a Member is a cheap, copyable value
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
