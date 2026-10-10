#pragma once

#include "CallResult.hpp"
#include "Object.hpp"

#include <array>
#include <bit>
#include <concepts>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>

namespace Iris
{
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

    /**
     * @brief What can be passed to a managed method or constructor: a PrimitiveValue, or an Object
     * (declared as `const Object&`), which matches any managed class or interface parameter.
     */
    template <typename T>
    concept Argument = PrimitiveValue<std::remove_cvref_t<T>> || std::same_as<std::remove_cvref_t<T>, Object>;

    /**
     * @brief An argument as declared in a method signature: a PrimitiveValue by value, or an Object as
     * `const Object&` (an Object cannot be copied, so a by-value Object is not a signature argument).
     */
    template <typename T>
    concept SignatureArgument = PrimitiveValue<T> || std::same_as<T, const Object&>;

    /**
     * @brief What a managed method can return: void, a PrimitiveValue, or `std::optional<Object>`, where
     * an empty optional is a null return.
     */
    template <typename T>
    concept Returnable = std::is_void_v<T> || PrimitiveValue<T> || std::same_as<T, std::optional<Object>>;

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
            Double = 11,
            Object = 12
        };

        template <typename Declared>
        constexpr ValueKind KindOf() noexcept
        {
            using T = std::remove_cvref_t<Declared>;
            if constexpr (std::is_void_v<T>)
                return ValueKind::Void;
            else if constexpr (std::same_as<T, Object> || std::same_as<T, std::optional<Object>>)
                return ValueKind::Object;
            else
            {
                static_assert(PrimitiveValue<T>,
                              "Only bool, sized integers, float, double and Object cross the boundary by value.");
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
        // float and double as their raw IEEE bits (float in the low 32 bits). An Object's slot is filled
        // in from its handle when the call is made.
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

        // Records one argument: its slot for a primitive, or the Object whose handle the call will use.
        template <typename Declared>
        void PackArgument(std::uint64_t& slot, const Object*& object, const Declared& value) noexcept
        {
            using T = std::remove_cvref_t<Declared>;
            if constexpr (std::same_as<T, Object>)
                object = &value;
            else
                slot = ToSlot<T>(value);
        }

        // An untyped method handle: arguments and result are raw slots. Use Method<Signature> instead.
        class RawMethod
        {
          public:
            // `objects[i]` is the Object for argument i, or null when `slots[i]` already holds the value.
            // Object arguments are checked (valid, same component) and their handles written into `slots`.
            [[nodiscard]] CallResult<std::uint64_t> Invoke(std::span<std::uint64_t> slots,
                                                           std::span<const Object* const> objects) const;
            [[nodiscard]] CallResult<std::uint64_t> InvokeInstance(const Object& target, std::span<std::uint64_t> slots,
                                                                   std::span<const Object* const> objects) const;

            // Takes ownership of an object handle the bridge returned from this method's component.
            [[nodiscard]] Object AdoptObject(std::uint64_t handle) const;

            // Why object arguments cannot be sent, or nothing: shared with Type::CreateObject.
            [[nodiscard]] static std::optional<std::pair<CallStatus, std::string>> ResolveObjects(
                std::uint64_t component, std::span<std::uint64_t> slots, std::span<const Object* const> objects);

          private:
            friend class ::Iris::Type;
            struct State;
            explicit RawMethod(std::shared_ptr<const State> state) noexcept;
            std::shared_ptr<const State> state;
        };

        template <typename Result>
        CallResult<Result> ToTyped(const RawMethod& method, CallResult<std::uint64_t> raw)
        {
            CallResult<Result> result;
            result.status = raw.status;
            result.diagnostic = std::move(raw.diagnostic);
            result.diagnosticTruncated = raw.diagnosticTruncated;
            if constexpr (!std::is_void_v<Result>)
            {
                if (raw.value)
                {
                    if constexpr (std::same_as<Result, std::optional<Object>>)
                    {
                        // Zero is a null return; anything else is a new, independently owned handle.
                        result.value.emplace(*raw.value == 0 ? std::nullopt
                                                             : std::optional<Object>(method.AdoptObject(*raw.value)));
                    }
                    else
                    {
                        result.value = FromSlot<Result>(*raw.value);
                    }
                }
            }
            return result;
        }
    } // namespace Detail

    template <typename Signature>
    class Method;

    /**
     * @brief A handle to a public static method, resolved by signature.
     *
     * `Method<std::int32_t(std::int32_t, std::int32_t)>` is a method `int Name(int, int)`. Found with
     * Type::FindMethod and called any number of times with Invoke, which checks nothing at runtime
     * beyond the handle: the signature was matched when the method was found. Like Type, a Method is a
     * cheap, copyable value that holds no managed reference and stops working (InvalidHandle) when its
     * component is released. A managed exception thrown by the method is ManagedException.
     *
     * An `Object` parameter (`const Object&`) matches any class or interface parameter, so the object's
     * real type is checked when the call is made (InvalidArgument if it does not fit). A method returning
     * an object is declared as returning `std::optional<Object>`: each call gets a new, independently
     * owned handle, even when the method returns the same managed object again, and an empty optional is
     * a null return.
     */
    template <typename... Args, typename Result>
        requires((SignatureArgument<Args> && ...) && Returnable<Result>)
    class Method<Result(Args...)>
    {
      public:
        [[nodiscard]] CallResult<Result> Invoke(const Args&... arguments) const
        {
            std::uint64_t slots[sizeof...(Args) + 1] = {};
            const Object* objects[sizeof...(Args) + 1] = {};
            std::size_t index = 0;
            ((Detail::PackArgument(slots[index], objects[index], arguments), ++index), ...);
            return Detail::ToTyped<Result>(method,
                                           method.Invoke(std::span<std::uint64_t>(slots, sizeof...(Args)),
                                                         std::span<const Object* const>(objects, sizeof...(Args))));
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
     * @brief A handle to a public instance method, resolved by signature.
     *
     * Found with Type::FindInstanceMethod and called on an Object that is an instance of the method's
     * declaring type (the type it was found on, or one of its subclasses). A virtual method dispatches
     * to the object's override. The object must belong to the same component, or the call is
     * InvalidArgument; a released object or component is InvalidHandle. Arguments and results follow the
     * rules of Method.
     */
    template <typename... Args, typename Result>
        requires((SignatureArgument<Args> && ...) && Returnable<Result>)
    class InstanceMethod<Result(Args...)>
    {
      public:
        [[nodiscard]] CallResult<Result> Invoke(const Object& object, const Args&... arguments) const
        {
            std::uint64_t slots[sizeof...(Args) + 1] = {};
            const Object* objects[sizeof...(Args) + 1] = {};
            std::size_t index = 0;
            ((Detail::PackArgument(slots[index], objects[index], arguments), ++index), ...);
            return Detail::ToTyped<Result>(
                method, method.InvokeInstance(object, std::span<std::uint64_t>(slots, sizeof...(Args)),
                                              std::span<const Object* const>(objects, sizeof...(Args))));
        }

      private:
        friend class Type;
        explicit InstanceMethod(Detail::RawMethod rawMethod) noexcept : method(std::move(rawMethod))
        {
        }
        Detail::RawMethod method;
    };
} // namespace Iris
