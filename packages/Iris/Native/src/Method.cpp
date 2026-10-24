#include "Iris/Method.hpp"

#include "ComponentCall.hpp"
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

    Detail::CallOrigin Detail::RawMethod::Origin() const noexcept
    {
        return CallOrigin(state->bridge, state->component);
    }

    namespace
    {
        // Invokes a static method (`target` is null) or an instance method on `target`; the two differ only in the
        // bridge function and the operation named in a failure.
        template <typename MethodState>
        CallResult<std::uint64_t> Run(const MethodState& method, const Object* target, std::span<std::uint64_t> slots,
                                      std::span<const Object* const> objects)
        {
            std::uint64_t targetHandle = 0;
            if (target != nullptr)
            {
                if (const auto problem = Detail::ComponentCall::TargetHandle(method.component, target, targetHandle))
                {
                    return Detail::Failure<std::uint64_t>(problem->first, problem->second);
                }
            }
            if (const auto problem = Detail::ComponentCall::ResolveObjects(method.component, slots, objects))
            {
                return Detail::Failure<std::uint64_t>(problem->first, problem->second);
            }
            if (slots.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
            {
                return Detail::Failure<std::uint64_t>(CallStatus::InvalidArgument, "Too many arguments.");
            }
            Detail::ManagedDiagnosticBuffer diagnostic;
            std::uint64_t result = 0;
            const auto count = static_cast<std::int32_t>(slots.size());
            const auto status =
                target == nullptr
                    ? method.bridge->invokeMethod(method.component, method.method, slots.data(), count, &result,
                                                  diagnostic.bytes.data(), diagnostic.Capacity, &diagnostic.written,
                                                  &diagnostic.truncated)
                    : method.bridge->invokeInstanceMethod(method.component, method.method, targetHandle, slots.data(),
                                                          count, &result, diagnostic.bytes.data(), diagnostic.Capacity,
                                                          &diagnostic.written, &diagnostic.truncated);
            return Detail::ComponentCall::CompleteReturningSlot<std::uint64_t>(
                *method.bridge, method.returnsBlock, status,
                target == nullptr ? "InvokeMethod" : "InvokeInstanceMethod", diagnostic, result);
        }
    } // namespace

    CallResult<std::uint64_t> Detail::RawMethod::Invoke(std::span<std::uint64_t> slots,
                                                        std::span<const Object* const> objects) const
    {
        return Run(*state, nullptr, slots, objects);
    }

    CallResult<std::uint64_t> Detail::RawMethod::InvokeInstance(const Object& object, std::span<std::uint64_t> slots,
                                                                std::span<const Object* const> objects) const
    {
        return Run(*state, &object, slots, objects);
    }
} // namespace Iris
