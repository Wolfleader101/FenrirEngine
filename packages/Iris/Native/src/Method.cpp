#include "Iris/Method.hpp"

#include "ManagedDiagnosticBuffer.hpp"
#include "MethodState.hpp"
#include "ObjectState.hpp"
#include "StringBlock.hpp"

#include <limits>
#include <utility>

namespace Iris
{
    Detail::RawMethod::RawMethod(std::shared_ptr<const State> methodState) noexcept : state(std::move(methodState))
    {
    }

    std::optional<std::pair<CallStatus, std::string>> Detail::RawMethod::ResolveObjects(
        std::uint64_t component, std::span<std::uint64_t> slots, std::span<const Object* const> objects)
    {
        for (std::size_t index = 0; index < objects.size() && index < slots.size(); ++index)
        {
            const Object* object = objects[index];
            if (object == nullptr)
            {
                continue;
            }
            if (!object->IsValid())
            {
                return std::make_pair(CallStatus::InvalidHandle, "Argument " + std::to_string(index) +
                                                                     " is an object that has been released or moved.");
            }
            if (object->state->component != component)
            {
                return std::make_pair(CallStatus::InvalidArgument,
                                      "Argument " + std::to_string(index) + " is an object from another component.");
            }
            slots[index] = object->state->object;
        }
        return std::nullopt;
    }

    Object Detail::RawMethod::AdoptObject(std::uint64_t handle) const
    {
        auto owned = std::make_unique<Object::State>();
        owned->bridge = state->bridge;
        owned->component = state->component;
        owned->object = handle;
        return Object(std::move(owned));
    }

    std::optional<std::string> Detail::RawMethod::TakeString(std::uint64_t block) const
    {
        return Detail::TakeString(*state->bridge, block);
    }

    void Detail::RawMethod::TakeStruct(std::uint64_t block, void* out, std::size_t size) const noexcept
    {
        Detail::TakeBytes(*state->bridge, block, out, size);
    }

    CallResult<std::uint64_t> Detail::RawMethod::Invoke(std::span<std::uint64_t> slots,
                                                        std::span<const Object* const> objects) const
    {
        if (const auto problem = ResolveObjects(state->component, slots, objects))
        {
            return Failure<std::uint64_t>(problem->first, problem->second);
        }
        if (slots.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        {
            return Failure<std::uint64_t>(CallStatus::InvalidArgument, "Too many arguments.");
        }
        ManagedDiagnosticBuffer diagnostic;
        std::uint64_t result = 0;
        const auto status = state->bridge->invokeMethod(
            state->component, state->method, slots.data(), static_cast<std::int32_t>(slots.size()), &result,
            diagnostic.bytes.data(), diagnostic.Capacity, &diagnostic.written, &diagnostic.truncated);
        try
        {
            return CompleteCall<std::uint64_t>(status, "InvokeMethod", diagnostic, result);
        }
        catch (...)
        {
            // The block is not owned by anything yet; give it back rather than leak it.
            if (state->returnsBlock && status == 0 && result != 0)
            {
                state->bridge->freeString(result);
            }
            throw;
        }
    }

    CallResult<std::uint64_t> Detail::RawMethod::InvokeInstance(const Object& object, std::span<std::uint64_t> slots,
                                                                std::span<const Object* const> objects) const
    {
        if (!object.IsValid())
        {
            return Failure<std::uint64_t>(CallStatus::InvalidHandle, "The object has been released or moved.");
        }
        if (object.state->component != state->component)
        {
            return Failure<std::uint64_t>(CallStatus::InvalidArgument, "The object belongs to another component.");
        }
        if (const auto problem = ResolveObjects(state->component, slots, objects))
        {
            return Failure<std::uint64_t>(problem->first, problem->second);
        }
        if (slots.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        {
            return Failure<std::uint64_t>(CallStatus::InvalidArgument, "Too many arguments.");
        }
        ManagedDiagnosticBuffer diagnostic;
        std::uint64_t result = 0;
        const auto status = state->bridge->invokeInstanceMethod(state->component, state->method, object.state->object,
                                                                slots.data(), static_cast<std::int32_t>(slots.size()),
                                                                &result, diagnostic.bytes.data(), diagnostic.Capacity,
                                                                &diagnostic.written, &diagnostic.truncated);
        try
        {
            return CompleteCall<std::uint64_t>(status, "InvokeInstanceMethod", diagnostic, result);
        }
        catch (...)
        {
            // The block is not owned by anything yet; give it back rather than leak it.
            if (state->returnsBlock && status == 0 && result != 0)
            {
                state->bridge->freeString(result);
            }
            throw;
        }
    }
} // namespace Iris
