#include "Iris/Method.hpp"

#include "ManagedDiagnosticBuffer.hpp"
#include "MethodState.hpp"

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
} // namespace Iris
