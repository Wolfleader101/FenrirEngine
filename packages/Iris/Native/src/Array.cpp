#include "Iris/Array.hpp"

#include "ComponentState.hpp"
#include "ManagedDiagnosticBuffer.hpp"
#include "ObjectState.hpp"
#include "StringBlock.hpp"
#include "StructRegistration.hpp"

#include <limits>

namespace Iris
{
    namespace
    {
        constexpr auto Released = "The array has been released or moved.";

        // Indexes and lengths cross the boundary as signed 64-bit values.
        bool FitsWire(std::size_t value)
        {
            return value <= static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max());
        }
    } // namespace

    CallResult<Object> Detail::RawArray::Create(const Component& component, const KindSpec& element, std::size_t length,
                                                std::int32_t& kind)
    {
        if (!component.IsLoaded())
        {
            return Failure<Object>(CallStatus::InvalidHandle, "The component has been released or moved.");
        }
        if (!FitsWire(length))
        {
            return Failure<Object>(CallStatus::InvalidArgument, "The array length is too large.");
        }
        const auto& bridge = *component.state->bridge;
        const auto wire =
            ResolveWireKind(bridge, component.state->token, KindSpec{ValueKind::Array, element.info, element.kind});
        if (!wire.Succeeded())
        {
            return PropagateFailure<Object>(wire);
        }
        ManagedDiagnosticBuffer diagnostic;
        std::uint64_t objectId = 0;
        const auto status = bridge.createArray(component.state->token, *wire.value, static_cast<std::int64_t>(length),
                                               &objectId, diagnostic.bytes.data(), diagnostic.Capacity,
                                               &diagnostic.written, &diagnostic.truncated);
        if (static_cast<CallStatus>(status) == CallStatus::Success && objectId == 0)
        {
            return Failure<Object>(CallStatus::InvalidResponse, "CreateArray returned the invalid handle 0.");
        }
        // The owner exists before the result is inspected, so a handle that arrives with a malformed
        // response is still released.
        auto owned = std::make_unique<Object::State>();
        owned->bridge = component.state->bridge;
        owned->component = component.state->token;
        owned->object = objectId;
        kind = *wire.value;
        return CompleteCall<Object>(status, "CreateArray", diagnostic, Object(std::move(owned)));
    }

    CallResult<std::int32_t> Detail::RawArray::ResolveKind(const Object& array, const KindSpec& element)
    {
        if (!array.IsValid())
        {
            return Failure<std::int32_t>(CallStatus::InvalidHandle, Released);
        }
        return ResolveWireKind(*array.state->bridge, array.state->component,
                               KindSpec{ValueKind::Array, element.info, element.kind});
    }

    CallResult<std::uint64_t> Detail::RawArray::Length(const Object& array)
    {
        if (!array.IsValid())
        {
            return Failure<std::uint64_t>(CallStatus::InvalidHandle, Released);
        }
        ManagedDiagnosticBuffer diagnostic;
        std::int64_t length = 0;
        const auto status = array.state->bridge->arrayLength(array.state->component, array.state->object, &length,
                                                             diagnostic.bytes.data(), diagnostic.Capacity,
                                                             &diagnostic.written, &diagnostic.truncated);
        if (static_cast<CallStatus>(status) == CallStatus::Success && length < 0)
        {
            return Failure<std::uint64_t>(CallStatus::InvalidResponse, "GetArrayLength returned a negative length.");
        }
        return CompleteCall<std::uint64_t>(status, "GetArrayLength", diagnostic, static_cast<std::uint64_t>(length));
    }

    CallResult<std::uint64_t> Detail::RawArray::Get(const Object& array, std::int32_t kind, std::size_t index,
                                                    bool returnsBlock)
    {
        if (!array.IsValid())
        {
            return Failure<std::uint64_t>(CallStatus::InvalidHandle, Released);
        }
        if (!FitsWire(index))
        {
            return Failure<std::uint64_t>(CallStatus::InvalidArgument, "The array index is too large.");
        }
        ManagedDiagnosticBuffer diagnostic;
        std::uint64_t slot = 0;
        const auto status = array.state->bridge->arrayGet(
            array.state->component, array.state->object, kind, static_cast<std::int64_t>(index), &slot,
            diagnostic.bytes.data(), diagnostic.Capacity, &diagnostic.written, &diagnostic.truncated);
        try
        {
            return CompleteCall<std::uint64_t>(status, "GetArrayElement", diagnostic, slot);
        }
        catch (...)
        {
            // A string or struct element is a block nothing owns yet; give it back rather than leak it.
            if (returnsBlock && status == 0 && slot != 0)
            {
                array.state->bridge->freeString(slot);
            }
            throw;
        }
    }

    CallResult<void> Detail::RawArray::Set(const Object& array, std::int32_t kind, std::size_t index,
                                           std::uint64_t slot)
    {
        if (!array.IsValid())
        {
            return Failure<void>(CallStatus::InvalidHandle, Released);
        }
        if (!FitsWire(index))
        {
            return Failure<void>(CallStatus::InvalidArgument, "The array index is too large.");
        }
        ManagedDiagnosticBuffer diagnostic;
        const auto status = array.state->bridge->arraySet(
            array.state->component, array.state->object, kind, static_cast<std::int64_t>(index), slot,
            diagnostic.bytes.data(), diagnostic.Capacity, &diagnostic.written, &diagnostic.truncated);
        return CompleteCall<void>(status, "SetArrayElement", diagnostic);
    }

    CallResult<void> Detail::RawArray::Copy(const Object& array, std::int32_t kind, std::size_t start,
                                            std::size_t count, void* buffer, bool intoArray)
    {
        if (!array.IsValid())
        {
            return Failure<void>(CallStatus::InvalidHandle, Released);
        }
        if (!FitsWire(start) || !FitsWire(count))
        {
            return Failure<void>(CallStatus::InvalidArgument, "The array range is too large.");
        }
        ManagedDiagnosticBuffer diagnostic;
        const auto status = array.state->bridge->arrayCopy(
            array.state->component, array.state->object, kind, static_cast<std::int64_t>(start),
            static_cast<std::int64_t>(count), buffer, intoArray ? 1 : 0, diagnostic.bytes.data(), diagnostic.Capacity,
            &diagnostic.written, &diagnostic.truncated);
        return CompleteCall<void>(status, "CopyArray", diagnostic);
    }

    std::optional<std::string> Detail::RawArray::TakeString(const Object& array, std::uint64_t block)
    {
        return Detail::TakeString(*array.state->bridge, block);
    }

    void Detail::RawArray::TakeStruct(const Object& array, std::uint64_t block, void* out, std::size_t size) noexcept
    {
        Detail::TakeBytes(*array.state->bridge, block, out, size);
    }
} // namespace Iris
