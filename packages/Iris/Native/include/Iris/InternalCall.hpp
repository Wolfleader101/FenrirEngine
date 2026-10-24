#pragma once

#include "CallResult.hpp"
#include "Method.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#if defined(_WIN32) && !defined(_WIN64)
#define IRIS_INTERNAL_CALL __cdecl
#else
#define IRIS_INTERNAL_CALL
#endif

namespace Iris
{
    // Not API: what the public templates need to talk to the bridge, including its wire format. It can change in
    // any release, so always build against the headers of the library you link.
    namespace Detail
    {
        // Stores a registration and, once the managed bridge is up, hands it over. See RegisterInternalCall.
        [[nodiscard]] CallResult<void> RegisterInternalCallRaw(std::string_view name, ValueKind returnKind,
                                                               std::span<const ValueKind> parameterKinds,
                                                               void* function);

        // Reports a C++ exception caught at an internal call boundary through the log sink.
        void ReportInternalCallException(const char* what) noexcept;

        // Reports, through the log sink, a string argument the boundary refused (a negative length, or a null pointer
        // with a length), so the function was not called.
        void ReportRefusedInternalCallString() noexcept;

        template <typename T>
        concept InternalCallResultType = std::is_void_v<T> || std::is_pointer_v<T> || PrimitiveValue<T>;

        // A parameter can also be a std::string_view: UTF-8 bytes, valid for the call.
        template <typename T>
        concept InternalCallParameterType =
            std::is_pointer_v<T> || PrimitiveValue<T> || std::is_same_v<T, std::string_view>;

        template <typename T>
            requires(InternalCallResultType<T> || InternalCallParameterType<T>)
        constexpr ValueKind InternalKindOf() noexcept
        {
            if constexpr (std::is_void_v<T>)
                return ValueKind::Void;
            else if constexpr (std::is_pointer_v<T>)
                return ValueKind::Pointer;
            else if constexpr (std::is_same_v<T, std::string_view>)
                return ValueKind::String;
            else
                return KindOf<T>();
        }

        template <typename Function>
        struct FunctionSignature;
        template <typename R, typename... Args>
        struct FunctionSignature<R (*)(Args...)>
        {
            using Type = R(Args...);
        };
        template <typename R, typename... Args>
        struct FunctionSignature<R (*)(Args...) noexcept>
        {
            using Type = R(Args...);
        };

        template <typename... T>
        struct TypeList
        {
        };

        template <typename... Lists>
        struct ConcatTypes;
        template <>
        struct ConcatTypes<>
        {
            using Type = TypeList<>;
        };
        template <typename... A>
        struct ConcatTypes<TypeList<A...>>
        {
            using Type = TypeList<A...>;
        };
        template <typename... A, typename... B, typename... Rest>
        struct ConcatTypes<TypeList<A...>, TypeList<B...>, Rest...>
        {
            using Type = typename ConcatTypes<TypeList<A..., B...>, Rest...>::Type;
        };

        // How a parameter crosses: its wire types, how many there are, whether the wire values starting at `Offset`
        // can become the parameter, and the conversion. Most parameters cross as themselves.
        template <typename T>
        struct WireOf
        {
            using Type = TypeList<T>;
            static constexpr std::size_t count = 1;

            template <std::size_t Offset, typename Wire>
            static constexpr bool Valid(const Wire&) noexcept
            {
                return true;
            }

            template <std::size_t Offset, typename Wire>
            static constexpr T From(const Wire& wire) noexcept
            {
                return std::get<Offset>(wire);
            }
        };

        // A string crosses as a pointer to its bytes followed by their length. A length of zero is an empty view
        // whatever the pointer; a negative length, or no bytes but a length, cannot become a view.
        template <>
        struct WireOf<std::string_view>
        {
            using Type = TypeList<const char*, std::int32_t>;
            static constexpr std::size_t count = 2;

            template <std::size_t Offset, typename Wire>
            static constexpr bool Valid(const Wire& wire) noexcept
            {
                const std::int32_t length = std::get<Offset + 1>(wire);
                return length == 0 || (length > 0 && std::get<Offset>(wire) != nullptr);
            }

            template <std::size_t Offset, typename Wire>
            static constexpr std::string_view From(const Wire& wire) noexcept
            {
                const std::int32_t length = std::get<Offset + 1>(wire);
                return length == 0 ? std::string_view()
                                   : std::string_view(std::get<Offset>(wire), static_cast<std::size_t>(length));
            }
        };

        // Where each parameter starts among the wire parameters.
        template <typename... Args>
        constexpr std::array<std::size_t, sizeof...(Args)> WireOffsets() noexcept
        {
            std::array<std::size_t, sizeof...(Args)> offsets{};
            std::size_t next = 0;
            std::size_t index = 0;
            ((offsets[index++] = next, next += WireOf<Args>::count), ...);
            return offsets;
        }

        // The function managed code actually calls, over the wire parameters. It refuses a string it cannot turn into
        // a std::string_view, runs `Function`, and turns a C++ exception into a logged error and a value-initialized
        // result, so nothing unwinds through managed frames.
        template <auto Function, typename R, typename Args, typename Wire>
        struct InternalCallThunk;
        template <auto Function, typename R, typename... Args, typename... Wire>
        struct InternalCallThunk<Function, R, TypeList<Args...>, TypeList<Wire...>>
        {
            static R IRIS_INTERNAL_CALL Call(Wire... wire) noexcept
            {
                return Run(std::tuple<Wire...>(wire...), std::index_sequence_for<Args...>{});
            }

          private:
            template <std::size_t... Index>
            static R Run(const std::tuple<Wire...>& wire, std::index_sequence<Index...>) noexcept
            {
                constexpr auto offsets = WireOffsets<Args...>();
                if (!(WireOf<Args>::template Valid<offsets[Index]>(wire) && ...))
                {
                    ReportRefusedInternalCallString();
                    return Default();
                }
                try
                {
                    if constexpr (std::is_void_v<R>)
                    {
                        Function(WireOf<Args>::template From<offsets[Index]>(wire)...);
                        return;
                    }
                    else
                    {
                        return Function(WireOf<Args>::template From<offsets[Index]>(wire)...);
                    }
                }
                catch (const std::exception& exception)
                {
                    ReportInternalCallException(exception.what());
                }
                catch (...)
                {
                    ReportInternalCallException(nullptr);
                }
                return Default();
            }

            static R Default() noexcept
            {
                if constexpr (!std::is_void_v<R>)
                {
                    return R{};
                }
            }
        };

        template <auto Function, typename R, typename... Args>
            requires(InternalCallResultType<R> && (InternalCallParameterType<Args> && ...))
        CallResult<void> RegisterInternalCallImpl(std::string_view name, std::type_identity<R(Args...)>)
        {
            using Wire = typename ConcatTypes<typename WireOf<Args>::Type...>::Type;
            const std::array<ValueKind, sizeof...(Args)> kinds = {InternalKindOf<Args>()...};
            return RegisterInternalCallRaw(
                name, InternalKindOf<R>(), std::span<const ValueKind>(kinds),
                reinterpret_cast<void*>(&InternalCallThunk<Function, R, TypeList<Args...>, Wire>::Call));
        }
    } // namespace Detail

    /**
     * @brief Makes a native function callable from managed component code.
     *
     * Component code declares a static unmanaged function pointer field,
     * `public static delegate* unmanaged[Cdecl]<int, int, int> Add;` in `MyComponent.Natives`, and the host
     * registers the function under the field's name:
     *
     * ```cpp
     * int AddImpl(int left, int right) { return left + right; }
     * Iris::RegisterInternalCall<&AddImpl>("MyComponent.Natives.Add");
     * ```
     *
     * The signature is taken from the function. Parameters and the result are void, sized integers,
     * `float`, `double`, `bool` (one byte), `char16_t` (a managed `char`) or pointers. A `bool` or `char16_t`
     * needs `[assembly: DisableRuntimeMarshalling]` on the component, or its load fails. A struct crosses as a
     * pointer to it, valid for the call only.
     *
     * A parameter (never the result) can also be a `std::string_view`: UTF-8 bytes, valid for the call only, and
     * not checked for being valid UTF-8. The managed field declares it as two parameters, a `byte*` to the bytes and
     * an `int` length, in that order; anything else for it fails the load. A length of zero arrives as an empty
     * view, whatever the pointer. A negative length, or a null pointer with a length, is refused at the boundary:
     * the function is not called, an Error is reported through the log sink, and managed code receives a
     * value-initialized result.
     *
     * Registration is process-wide, may happen before or after the host starts, and applies to every
     * component loaded afterwards (already loaded components are not changed). Registering the same name
     * with the same function again succeeds; a different function under a used name is InvalidArgument.
     * There is no unregistering: a function must stay valid for the life of the process.
     *
     * When a component loads, the bridge binds every public static unmanaged function pointer field in its
     * assembly by the name `Namespace.Type.Field` (`Namespace.Outer+Inner.Field` for a nested type) and
     * checks the signature. A field with no registration, a different signature, a calling convention
     * other than Cdecl, or that is readonly, thread-static or on a generic type fails the load with
     * InvalidArgument and a diagnostic naming every such field, so managed code never calls a null pointer.
     * Private and managed function pointer fields, and fields in dependency assemblies, are ignored. Binding
     * runs the declaring type's static constructor (types without one are bound first), so a static
     * constructor must not call an internal call. `nint`/`nuint` fields are pointers; use `size_t` as a
     * 64-bit integer only if the managed side says `ulong`. Bindings hold no managed reference, so they
     * never keep a released component alive.
     *
     * A C++ exception that escapes `Function` is caught at the boundary and never unwinds through managed
     * frames. It is reported as an Error through the log sink, and managed code receives a
     * value-initialized result (zero, or null for a pointer). Functions are called on managed threads and
     * must be thread-safe if components run on several.
     */
    template <auto Function>
        requires(std::is_pointer_v<decltype(Function)> && std::is_function_v<std::remove_pointer_t<decltype(Function)>>)
    [[nodiscard]] CallResult<void> RegisterInternalCall(std::string_view name)
    {
        using Signature = typename Detail::FunctionSignature<std::remove_cv_t<decltype(Function)>>::Type;
        return Detail::RegisterInternalCallImpl<Function>(name, std::type_identity<Signature>{});
    }
} // namespace Iris
