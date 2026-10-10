#pragma once

#include "CallResult.hpp"
#include "Method.hpp"

#include <array>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace Iris
{
    struct TypeInfo
    {
        std::string fullName;
        std::string assemblyName;
    };

    namespace Detail
    {
        template <typename Signature>
        struct MethodTraits;

        template <typename Result, typename... Args>
        struct MethodTraits<Result(Args...)>
        {
            using Handle = Method<Result(Args...)>;
            static constexpr ValueKind returnKind = KindOf<Result>();
            static constexpr std::array<ValueKind, sizeof...(Args)> parameterKinds = {KindOf<Args>()...};
        };
    } // namespace Detail

    /**
     * @brief A handle to a managed type found in a Component.
     *
     * Cheap to copy; it holds no managed reference. The handle is valid while its Component is loaded:
     * after the component is released, every call returns InvalidHandle. Handles are never reused, and
     * a handle only works with the component it was found in. It does not keep the component alive and
     * may outlive both the Component owner object and the Host.
     */
    class Type
    {
      public:
        [[nodiscard]] CallResult<TypeInfo> GetInfo() const;

        /**
         * @brief Finds a public static, non-generic method by name and exact signature.
         *
         * `FindMethod<std::int32_t(std::int32_t, std::int32_t)>("Add")` finds `int Add(int, int)`;
         * parameter and return types are derived from the C++ types, and only PrimitiveValue types (and
         * void for the return) compile. Overloads are told apart by their exact parameter types. A method
         * with another name, return type, parameter types or count, a generic or UnmanagedCallersOnly
         * method, or one that is not public is NotFound. The name is case-sensitive UTF-8 without null
         * characters, borrowed for the call. Finding the same method again returns an equivalent handle.
         */
        template <typename Signature>
        [[nodiscard]] CallResult<typename Detail::MethodTraits<Signature>::Handle> FindMethod(
            std::string_view name) const;

      private:
        friend class Component;
        struct State;
        explicit Type(std::shared_ptr<const State> state) noexcept;
        [[nodiscard]] CallResult<Detail::RawMethod> FindMethodRaw(
            std::string_view name, Detail::ValueKind returnKind,
            std::span<const Detail::ValueKind> parameterKinds) const;
        std::shared_ptr<const State> state;
    };

    template <typename Signature>
    CallResult<typename Detail::MethodTraits<Signature>::Handle> Type::FindMethod(std::string_view name) const
    {
        using Traits = Detail::MethodTraits<Signature>;
        auto raw = FindMethodRaw(name, Traits::returnKind, std::span<const Detail::ValueKind>(Traits::parameterKinds));
        CallResult<typename Traits::Handle> result;
        result.status = raw.status;
        result.diagnostic = std::move(raw.diagnostic);
        result.diagnosticTruncated = raw.diagnosticTruncated;
        if (raw.value)
        {
            result.value.emplace(typename Traits::Handle(std::move(*raw.value)));
        }
        return result;
    }
} // namespace Iris
