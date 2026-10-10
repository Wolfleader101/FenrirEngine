#include "Iris/Method.hpp"

#include "ManagedDiagnosticBuffer.hpp"
#include "MethodState.hpp"
#include "ObjectState.hpp"

#include <limits>
#include <utility>

namespace Iris
{
    Detail::RawMethod::RawMethod(std::shared_ptr<const State> methodState) noexcept : state(std::move(methodState))
    {
    }

    CallResult<std::uint64_t> Detail::RawMethod::Invoke(std::span<const std::uint64_t> arguments) const
    {
        if (arguments.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        {
            return Failure<std::uint64_t>(CallStatus::InvalidArgument, "Too many arguments.");
        }
        ManagedDiagnosticBuffer diagnostic;
        std::uint64_t result = 0;
        const auto status = state->bridge->invokeMethod(
            state->component, state->method, arguments.data(), static_cast<std::int32_t>(arguments.size()), &result,
            diagnostic.bytes.data(), diagnostic.Capacity, &diagnostic.written, &diagnostic.truncated);
        return CompleteCall<std::uint64_t>(status, "InvokeMethod", diagnostic, result);
    }

    CallResult<std::uint64_t> Detail::RawMethod::InvokeInstance(const Object& object,
                                                                std::span<const std::uint64_t> arguments) const
    {
        if (!object.IsValid())
        {
            return Failure<std::uint64_t>(CallStatus::InvalidHandle, "The object has been released or moved.");
        }
        if (object.state->component != state->component)
        {
            return Failure<std::uint64_t>(CallStatus::InvalidArgument, "The object belongs to another component.");
        }
        if (arguments.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        {
            return Failure<std::uint64_t>(CallStatus::InvalidArgument, "Too many arguments.");
        }
        ManagedDiagnosticBuffer diagnostic;
        std::uint64_t result = 0;
        const auto status = state->bridge->invokeInstanceMethod(
            state->component, state->method, object.state->object, arguments.data(),
            static_cast<std::int32_t>(arguments.size()), &result, diagnostic.bytes.data(), diagnostic.Capacity,
            &diagnostic.written, &diagnostic.truncated);
        return CompleteCall<std::uint64_t>(status, "InvokeInstanceMethod", diagnostic, result);
    }
} // namespace Iris
