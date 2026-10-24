#pragma once

#include "CallResult.hpp"
#include "Component.hpp"
#include "Method.hpp"
#include "Object.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Iris
{
    // Not API: what the public templates need to talk to the bridge, including its wire format. It can change in
    // any release, so always build against the headers of the library you link.
    namespace Detail
    {
        // The same status and diagnostic, for a different result type.
        template <typename To, typename From>
        CallResult<To> Rewrap(const CallResult<From>& from)
        {
            CallResult<To> result;
            result.status = from.status;
            result.diagnostic = from.diagnostic;
            result.diagnosticTruncated = from.diagnosticTruncated;
            return result;
        }

        // The untyped part of Array<T>: everything that talks to the bridge. `kind` is the array's wire kind.
        class RawArray
        {
          public:
            // Creates a managed array of `spec`'s element type and `length` elements. The wire kind is
            // written to `kind` (a struct element type is registered and validated first).
            [[nodiscard]] static CallResult<Object> Create(const Component& component, const KindSpec& element,
                                                           std::size_t length, std::int32_t& kind);
            // The wire kind of an array with the given element spec, for an array this object stands for.
            [[nodiscard]] static CallResult<std::int32_t> ResolveKind(const Object& array, const KindSpec& element);

            [[nodiscard]] static CallResult<std::uint64_t> Length(const Object& array);
            // The element's slot: bits for a primitive, a block for a string or struct (see CallOrigin).
            [[nodiscard]] static CallResult<std::uint64_t> Get(const Object& array, std::int32_t kind,
                                                               std::size_t index, bool returnsBlock);
            [[nodiscard]] static CallResult<void> Set(const Object& array, std::int32_t kind, std::size_t index,
                                                      std::uint64_t slot);
            // Bulk copy of `count` elements starting at `start`, between the array and `buffer`.
            [[nodiscard]] static CallResult<void> Copy(const Object& array, std::int32_t kind, std::size_t start,
                                                       std::size_t count, void* buffer, bool intoArray);
            // Where the array's element results go (see CallOrigin).
            [[nodiscard]] static CallOrigin Origin(const Object& array) noexcept;
        };
    } // namespace Detail

    /**
     * @brief Owns a one-dimensional managed array `T[]`, with the ownership rules of an Object.
     *
     * Created with Array<T>::Create, or returned from a managed method as `std::optional<Array<T>>`
     * (an empty optional is a null array, distinct from an empty one). Passed to a method as
     * `const Array<T>&` (never null) or `const Array<T>*` (null allowed). Like an Object, an Array keeps
     * the managed array alive and blocks Component::Release (InUse) until released or destroyed; moving
     * transfers ownership. The same managed array returned twice gives two independently owned handles
     * that share the elements.
     *
     * Elements: a primitive or struct element is read and written by value; a string element is
     * `std::optional<std::string>` on the way out and `std::optional<std::string_view>` on the way in
     * (null is distinct from empty). An index outside the array is InvalidArgument with a diagnostic.
     * CopyOut and CopyIn move a range in one call for primitive and struct elements.
     */
    template <ArrayElement T>
    class Array
    {
      public:
        using Element = std::conditional_t<std::same_as<T, std::string>, std::optional<std::string>, T>;
        using ElementIn = std::conditional_t<std::same_as<T, std::string>, std::optional<std::string_view>, const T&>;

        [[nodiscard]] static CallResult<Array> Create(const Component& component, std::size_t length)
        {
            std::int32_t kind = 0;
            auto raw = Detail::RawArray::Create(component, Detail::ElementSpecOf<T>(), length, kind);
            CallResult<Array> result;
            result.status = raw.status;
            result.diagnostic = std::move(raw.diagnostic);
            result.diagnosticTruncated = raw.diagnosticTruncated;
            if (raw.value)
            {
                result.value.emplace(Array(std::move(*raw.value), kind));
            }
            return result;
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return object.IsValid();
        }
        [[nodiscard]] CallResult<void> Release()
        {
            return object.Release();
        }

        [[nodiscard]] CallResult<std::size_t> Length() const
        {
            auto raw = Detail::RawArray::Length(object);
            CallResult<std::size_t> result;
            result.status = raw.status;
            result.diagnostic = std::move(raw.diagnostic);
            result.diagnosticTruncated = raw.diagnosticTruncated;
            if (raw.value)
            {
                result.value = static_cast<std::size_t>(*raw.value);
            }
            return result;
        }

        [[nodiscard]] CallResult<Element> Get(std::size_t index) const
        {
            const auto arrayKind = Kind();
            if (!arrayKind.Succeeded())
            {
                return Detail::Rewrap<Element>(arrayKind);
            }
            auto raw = Detail::RawArray::Get(object, *arrayKind.value, index, !PrimitiveValue<T>);
            CallResult<Element> result;
            result.status = raw.status;
            result.diagnostic = std::move(raw.diagnostic);
            result.diagnosticTruncated = raw.diagnosticTruncated;
            if (raw.value)
            {
                if constexpr (std::same_as<T, std::string>)
                {
                    result.value.emplace(Detail::RawArray::Origin(object).TakeString(*raw.value));
                }
                else if constexpr (StructValue<T>)
                {
                    T value;
                    Detail::RawArray::Origin(object).TakeStruct(*raw.value, &value, sizeof(T));
                    result.value = value;
                }
                else
                {
                    result.value = Detail::FromSlot<T>(*raw.value);
                }
            }
            return result;
        }

        [[nodiscard]] CallResult<void> Set(std::size_t index, ElementIn value) const
        {
            const auto arrayKind = Kind();
            if (!arrayKind.Succeeded())
            {
                return Detail::Rewrap<void>(arrayKind);
            }
            if constexpr (std::same_as<T, std::string>)
            {
                const Detail::StringArgumentView view = Detail::MakeStringView(value);
                return Detail::RawArray::Set(object, *arrayKind.value, index, reinterpret_cast<std::uintptr_t>(&view));
            }
            else if constexpr (StructValue<T>)
            {
                return Detail::RawArray::Set(object, *arrayKind.value, index, reinterpret_cast<std::uintptr_t>(&value));
            }
            else
            {
                return Detail::RawArray::Set(object, *arrayKind.value, index, Detail::ToSlot<T>(value));
            }
        }

        // Copies `out.size()` elements starting at `start` into `out`. Primitive and struct elements only.
        [[nodiscard]] CallResult<void> CopyOut(std::size_t start, std::span<T> out) const
            requires(!std::same_as<T, std::string>)
        {
            const auto arrayKind = Kind();
            if (!arrayKind.Succeeded())
            {
                return Detail::Rewrap<void>(arrayKind);
            }
            return Detail::RawArray::Copy(object, *arrayKind.value, start, out.size(), out.data(), false);
        }

        // Copies `in.size()` elements into the array starting at `start`. Primitive and struct elements only.
        [[nodiscard]] CallResult<void> CopyIn(std::size_t start, std::span<const T> in) const
            requires(!std::same_as<T, std::string>)
        {
            const auto arrayKind = Kind();
            if (!arrayKind.Succeeded())
            {
                return Detail::Rewrap<void>(arrayKind);
            }
            return Detail::RawArray::Copy(object, *arrayKind.value, start, in.size(), const_cast<T*>(in.data()), true);
        }

      private:
        friend struct Detail::ArrayAccess;
        Array(Object owner, std::int32_t arrayKind) noexcept : object(std::move(owner)), cachedKind(arrayKind)
        {
        }

        // The array's wire kind: known from creation, otherwise worked out (and a struct element checked
        // against the component) the first time an element is used.
        [[nodiscard]] CallResult<std::int32_t> Kind() const
        {
            if (cachedKind != 0)
            {
                CallResult<std::int32_t> known;
                known.status = CallStatus::Success;
                known.value = cachedKind;
                return known;
            }
            auto resolved = Detail::RawArray::ResolveKind(object, Detail::ElementSpecOf<T>());
            if (resolved.value)
            {
                cachedKind = *resolved.value;
            }
            return resolved;
        }

        Object object;
        mutable std::int32_t cachedKind;
    };
} // namespace Iris
