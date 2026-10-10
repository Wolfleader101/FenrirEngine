#pragma once

#include "CallResult.hpp"
#include "Method.hpp"
#include "Object.hpp"

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
            using InstanceHandle = InstanceMethod<Result(Args...)>;
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

        /**
         * @brief Finds a public instance method by name and exact signature, possibly inherited.
         *
         * Like FindMethod, but the method is called on an Object (see InstanceMethod). A virtual method
         * found here dispatches to the override of the object's real type.
         */
        template <typename Signature>
        [[nodiscard]] CallResult<typename Detail::MethodTraits<Signature>::InstanceHandle> FindInstanceMethod(
            std::string_view name) const;

        /**
         * @brief Constructs an object with the public constructor whose parameters match exactly.
         *
         * `CreateObject(std::int32_t{5}, 2.5)` calls `T(int, double)`; no arguments call the parameterless
         * constructor. Only PrimitiveValue arguments compile. An abstract or static type, or no matching
         * constructor, is NotFound; an exception from the constructor is ManagedException and leaves
         * nothing rooted. The returned Object owns the new instance.
         */
        template <PrimitiveValue... Args>
        [[nodiscard]] CallResult<Object> CreateObject(Args... arguments) const;

      private:
        friend class Component;
        struct State;
        explicit Type(std::shared_ptr<const State> state) noexcept;
        [[nodiscard]] CallResult<Detail::RawMethod> FindMethodRaw(std::string_view name, Detail::ValueKind returnKind,
                                                                  std::span<const Detail::ValueKind> parameterKinds,
                                                                  bool isStatic) const;
        [[nodiscard]] CallResult<Object> CreateObjectRaw(std::span<const Detail::ValueKind> parameterKinds,
                                                         std::span<const std::uint64_t> arguments) const;
        std::shared_ptr<const State> state;
    };

    template <typename Signature>
    CallResult<typename Detail::MethodTraits<Signature>::Handle> Type::FindMethod(std::string_view name) const
    {
        using Traits = Detail::MethodTraits<Signature>;
        auto raw =
            FindMethodRaw(name, Traits::returnKind, std::span<const Detail::ValueKind>(Traits::parameterKinds), true);
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

    template <typename Signature>
    CallResult<typename Detail::MethodTraits<Signature>::InstanceHandle> Type::FindInstanceMethod(
        std::string_view name) const
    {
        using Traits = Detail::MethodTraits<Signature>;
        auto raw =
            FindMethodRaw(name, Traits::returnKind, std::span<const Detail::ValueKind>(Traits::parameterKinds), false);
        CallResult<typename Traits::InstanceHandle> result;
        result.status = raw.status;
        result.diagnostic = std::move(raw.diagnostic);
        result.diagnosticTruncated = raw.diagnosticTruncated;
        if (raw.value)
        {
            result.value.emplace(typename Traits::InstanceHandle(std::move(*raw.value)));
        }
        return result;
    }

    template <PrimitiveValue... Args>
    CallResult<Object> Type::CreateObject(Args... arguments) const
    {
        const std::array<Detail::ValueKind, sizeof...(Args)> kinds = {Detail::KindOf<Args>()...};
        const std::uint64_t slots[sizeof...(Args) + 1] = {Detail::ToSlot<Args>(arguments)..., 0};
        return CreateObjectRaw(std::span<const Detail::ValueKind>(kinds),
                               std::span<const std::uint64_t>(slots, sizeof...(Args)));
    }
} // namespace Iris
