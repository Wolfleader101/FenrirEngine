#include "Iris/Member.hpp"

#include "ComponentCall.hpp"
#include "ManagedDiagnosticBuffer.hpp"
#include "MemberState.hpp"

#include <utility>

namespace Iris
{
    Detail::RawMember::RawMember(std::shared_ptr<const State> memberState) noexcept : state(std::move(memberState))
    {
    }

    Detail::CallOrigin Detail::RawMember::Origin() const noexcept
    {
        return CallOrigin(state->bridge, state->component);
    }

    CallResult<std::uint64_t> Detail::RawMember::Get(const Object* target) const
    {
        std::uint64_t targetHandle = 0;
        if (const auto problem = ComponentCall::TargetHandle(state->component, target, targetHandle))
        {
            return Failure<std::uint64_t>(problem->first, problem->second);
        }
        ManagedDiagnosticBuffer diagnostic;
        std::uint64_t value = 0;
        const auto status =
            state->bridge->getMember(state->component, state->member, targetHandle, &value, diagnostic.bytes.data(),
                                     diagnostic.Capacity, &diagnostic.written, &diagnostic.truncated);
        return ComponentCall::CompleteReturningSlot<std::uint64_t>(*state->bridge, state->returnsBlock, status,
                                                                   "GetMember", diagnostic, value);
    }

    CallResult<void> Detail::RawMember::Set(const Object* target, std::uint64_t value, const Object* objectValue) const
    {
        std::uint64_t targetHandle = 0;
        if (const auto problem = ComponentCall::TargetHandle(state->component, target, targetHandle))
        {
            return Failure<void>(problem->first, problem->second);
        }
        if (objectValue != nullptr)
        {
            std::uint64_t slot = 0;
            const Object* objects[1] = {objectValue};
            if (const auto problem =
                    ComponentCall::ResolveObjects(state->component, std::span<std::uint64_t>(&slot, 1), objects))
            {
                return Failure<void>(problem->first, problem->second);
            }
            value = slot;
        }
        ManagedDiagnosticBuffer diagnostic;
        const auto status =
            state->bridge->setMember(state->component, state->member, targetHandle, value, diagnostic.bytes.data(),
                                     diagnostic.Capacity, &diagnostic.written, &diagnostic.truncated);
        return CompleteCall<void>(status, "SetMember", diagnostic);
    }
} // namespace Iris
