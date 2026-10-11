#pragma once

#include "CallResult.hpp"
#include "Method.hpp"

#include <array>
#include <exception>
#include <span>
#include <string_view>
#include <type_traits>

#if defined(_WIN32) && !defined(_WIN64)
#define IRIS_INTERNAL_CALL __cdecl
#else
#define IRIS_INTERNAL_CALL
#endif

namespace Iris
{
    namespace Detail
    {
        // Stores a registration and, once the managed bridge is up, hands it over. See RegisterInternalCall.
        [[nodiscard]] CallResult<void> RegisterInternalCallRaw(std::string_view name, ValueKind returnKind,
                                                               std::span<const ValueKind> parameterKinds,
                                                               void* function);

        // Reports a C++ exception caught at an internal call boundary through the log sink.
        void ReportInternalCallException(const char* what) noexcept;

        template <typename T>
        concept InternalCallType =
            std::is_void_v<T> || std::is_pointer_v<T> || (PrimitiveValue<T> && !std::same_as<T, bool>);

        template <InternalCallType T>
        constexpr ValueKind InternalKindOf() noexcept
        {
            if constexpr (std::is_void_v<T>)
                return ValueKind::Void;
            else if constexpr (std::is_pointer_v<T>)
                return ValueKind::Pointer;
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

        // The function managed code actually calls: it runs `Function` and turns a C++ exception into a
        // logged error and a value-initialized result, so nothing unwinds through managed frames.
        template <auto Function, typename Signature>
        struct InternalCallThunk;
        template <auto Function, typename R, typename... Args>
        struct InternalCallThunk<Function, R(Args...)>
        {
            static R IRIS_INTERNAL_CALL Call(Args... arguments) noexcept
            {
                try
                {
                    if constexpr (std::is_void_v<R>)
                    {
                        Function(arguments...);
                        return;
                    }
                    else
                    {
                        return Function(arguments...);
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
                if constexpr (!std::is_void_v<R>)
                {
                    return R{};
                }
            }
        };

        template <auto Function, typename R, typename... Args>
            requires(InternalCallType<R> && (InternalCallType<Args> && ...))
        CallResult<void> RegisterInternalCallImpl(std::string_view name, std::type_identity<R(Args...)>)
        {
            const std::array<ValueKind, sizeof...(Args)> kinds = {InternalKindOf<Args>()...};
            return RegisterInternalCallRaw(name, InternalKindOf<R>(), std::span<const ValueKind>(kinds),
                                           reinterpret_cast<void*>(&InternalCallThunk<Function, R(Args...)>::Call));
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
     * `float`, `double`, or pointers (a `bool` or character is rejected, because its unmanaged
     * representation is ambiguous). A string crosses as a UTF-8 `const char*` and a length, and a struct
     * as a pointer to it; the pointers are valid for the call only.
     *
     * Registration is process-wide, may happen before or after the host starts, and applies to every
     * component loaded afterwards (already loaded components are not changed). Registering the same name
     * with the same function again succeeds; a different function under a used name is InvalidArgument.
     * There is no unregistering: a function must stay valid for the life of the process.
     *
     * When a component loads, the bridge binds every static function pointer field in its assembly by the
     * name `Namespace.Type.Field` and checks the signature. A field with no registration, a different
     * signature, a calling convention other than Cdecl, a readonly field or a managed function pointer fails
     * the load with InvalidArgument and a diagnostic naming every such field, so managed code never calls a
     * null pointer. Bindings hold no managed reference, so they never keep a released component alive.
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
