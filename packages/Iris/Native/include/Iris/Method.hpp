#pragma once

#include "CallResult.hpp"

#include <array>
#include <bit>
#include <concepts>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

namespace Iris
{
    class Object;
    class Type;

    /**
     * @brief The value types that cross the boundary by value: bool, 8/16/32/64-bit integers, float
     * and double. Character types are excluded because their managed meaning is ambiguous.
     */
    template <typename T>
    concept PrimitiveValue = std::same_as<T, bool> || std::same_as<T, float> || std::same_as<T, double> ||
                             (std::integral<T> && !std::same_as<T, char> && !std::same_as<T, wchar_t> &&
                              !std::same_as<T, char8_t> && !std::same_as<T, char16_t> && !std::same_as<T, char32_t> &&
                              (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8));

    namespace Detail
    {
        // Wire values, mirrored by the managed ValueKind. Void is a return type only.
        enum class ValueKind : std::int32_t
        {
            Void = 0,
            Bool = 1,
            Int8 = 2,
            UInt8 = 3,
            Int16 = 4,
            UInt16 = 5,
            Int32 = 6,
            UInt32 = 7,
            Int64 = 8,
            UInt64 = 9,
            Float = 10,
            Double = 11
        };

        template <typename T>
        constexpr ValueKind KindOf() noexcept
        {
            if constexpr (std::is_void_v<T>)
                return ValueKind::Void;
            else
            {
                static_assert(PrimitiveValue<T>,
                              "Only bool, sized integers, float and double cross the boundary by value.");
                if constexpr (std::same_as<T, bool>)
                    return ValueKind::Bool;
                else if constexpr (std::same_as<T, float>)
                    return ValueKind::Float;
                else if constexpr (std::same_as<T, double>)
                    return ValueKind::Double;
                else if constexpr (sizeof(T) == 1)
                    return std::is_signed_v<T> ? ValueKind::Int8 : ValueKind::UInt8;
                else if constexpr (sizeof(T) == 2)
                    return std::is_signed_v<T> ? ValueKind::Int16 : ValueKind::UInt16;
                else if constexpr (sizeof(T) == 4)
                    return std::is_signed_v<T> ? ValueKind::Int32 : ValueKind::UInt32;
                else
                    return std::is_signed_v<T> ? ValueKind::Int64 : ValueKind::UInt64;
            }
        }

        // Each value travels in one 64-bit slot: integers sign- or zero-extended, bool as 0 or 1, and
        // float and double as their raw IEEE bits (float in the low 32 bits).
        template <PrimitiveValue T>
        constexpr std::uint64_t ToSlot(T value) noexcept
        {
            if constexpr (std::same_as<T, float>)
                return std::bit_cast<std::uint32_t>(value);
            else if constexpr (std::same_as<T, double>)
                return std::bit_cast<std::uint64_t>(value);
            else if constexpr (std::same_as<T, bool>)
                return value ? 1U : 0U;
            else if constexpr (std::is_signed_v<T>)
                return static_cast<std::uint64_t>(static_cast<std::int64_t>(value));
            else
                return static_cast<std::uint64_t>(value);
        }

        template <PrimitiveValue T>
        constexpr T FromSlot(std::uint64_t slot) noexcept
        {
            if constexpr (std::same_as<T, float>)
                return std::bit_cast<float>(static_cast<std::uint32_t>(slot));
            else if constexpr (std::same_as<T, double>)
                return std::bit_cast<double>(slot);
            else if constexpr (std::same_as<T, bool>)
                return slot != 0;
            else
                return static_cast<T>(slot);
        }

        // An untyped method handle: arguments and result are raw slots. Use Method<Signature> instead.
        class RawMethod
        {
          public:
            [[nodiscard]] CallResult<std::uint64_t> Invoke(std::span<const std::uint64_t> arguments) const;
            [[nodiscard]] CallResult<std::uint64_t> InvokeInstance(const Object& object,
                                                                   std::span<const std::uint64_t> arguments) const;

          private:
            friend class ::Iris::Type;
            struct State;
            explicit RawMethod(std::shared_ptr<const State> state) noexcept;
            std::shared_ptr<const State> state;
        };

        template <typename Result>
        CallResult<Result> ToTyped(CallResult<std::uint64_t> raw)
        {
            CallResult<Result> result;
            result.status = raw.status;
            result.diagnostic = std::move(raw.diagnostic);
            result.diagnosticTruncated = raw.diagnosticTruncated;
            if constexpr (!std::is_void_v<Result>)
            {
                if (raw.value)
                {
                    result.value = FromSlot<Result>(*raw.value);
                }
            }
            return result;
        }
    } // namespace Detail

    template <typename Signature>
    class Method;

    /**
     * @brief A handle to a public static method, resolved by exact signature.
     *
     * `Method<std::int32_t(std::int32_t, std::int32_t)>` is a method `int Name(int, int)`. Found with
     * Type::FindMethod and called any number of times with Invoke, which checks nothing at runtime
     * beyond the handle: the signature was matched when the method was found. Like Type, a Method is a
     * cheap, copyable value that holds no managed reference and stops working (InvalidHandle) when its
     * component is released. A managed exception thrown by the method is ManagedException.
     */
    template <PrimitiveValue... Args, typename Result>
        requires(std::is_void_v<Result> || PrimitiveValue<Result>)
    class Method<Result(Args...)>
    {
      public:
        [[nodiscard]] CallResult<Result> Invoke(Args... arguments) const
        {
            const std::uint64_t slots[sizeof...(Args) + 1] = {Detail::ToSlot<Args>(arguments)..., 0};
            return Detail::ToTyped<Result>(method.Invoke(std::span<const std::uint64_t>(slots, sizeof...(Args))));
        }

      private:
        friend class Type;
        explicit Method(Detail::RawMethod rawMethod) noexcept : method(std::move(rawMethod))
        {
        }
        Detail::RawMethod method;
    };

    template <typename Signature>
    class InstanceMethod;

    /**
     * @brief A handle to a public instance method, resolved by exact signature.
     *
     * Found with Type::FindInstanceMethod and called on an Object that is an instance of the method's
     * declaring type (the type it was found on, or one of its subclasses). A virtual method dispatches
     * to the object's override. The object must belong to the same component, or the call is
     * InvalidArgument; a released object or component is InvalidHandle. Otherwise it behaves like
     * Method: a cheap, copyable value that holds no managed reference.
     */
    template <PrimitiveValue... Args, typename Result>
        requires(std::is_void_v<Result> || PrimitiveValue<Result>)
    class InstanceMethod<Result(Args...)>
    {
      public:
        [[nodiscard]] CallResult<Result> Invoke(const Object& object, Args... arguments) const
        {
            const std::uint64_t slots[sizeof...(Args) + 1] = {Detail::ToSlot<Args>(arguments)..., 0};
            return Detail::ToTyped<Result>(
                method.InvokeInstance(object, std::span<const std::uint64_t>(slots, sizeof...(Args))));
        }

      private:
        friend class Type;
        explicit InstanceMethod(Detail::RawMethod rawMethod) noexcept : method(std::move(rawMethod))
        {
        }
        Detail::RawMethod method;
    };
} // namespace Iris
