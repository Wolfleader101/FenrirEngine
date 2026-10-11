#include "Iris/Member.hpp"

#include "ManagedDiagnosticBuffer.hpp"
#include "MemberState.hpp"
#include "ObjectState.hpp"
#include "StringBlock.hpp"

#include <utility>

namespace Iris
{
    std::optional<std::pair<CallStatus, std::string>> Detail::RawMember::TargetHandle(std::uint64_t component,
                                                                                      const Object* target,
                                                                                      std::uint64_t& handle)
    {
        handle = 0;
        if (target == nullptr)
        {
            return std::nullopt;
        }
        if (!target->IsValid())
        {
            return std::make_pair(CallStatus::InvalidHandle, std::string("The object has been released or moved."));
        }
        if (target->state->component != component)
        {
            return std::make_pair(CallStatus::InvalidArgument, std::string("The object belongs to another component."));
        }
        handle = target->state->object;
        return std::nullopt;
    }

    Detail::RawMember::RawMember(std::shared_ptr<const State> memberState) noexcept : state(std::move(memberState))
    {
    }

    Object Detail::RawMember::AdoptObject(std::uint64_t handle) const
    {
        auto owned = std::make_unique<Object::State>();
        owned->bridge = state->bridge;
        owned->component = state->component;
        owned->object = handle;
        return Object(std::move(owned));
    }

    std::optional<std::string> Detail::RawMember::TakeString(std::uint64_t block) const
    {
        return Detail::TakeString(*state->bridge, block);
    }

    void Detail::RawMember::TakeStruct(std::uint64_t block, void* out, std::size_t size) const noexcept
    {
        Detail::TakeBytes(*state->bridge, block, out, size);
    }

    CallResult<std::uint64_t> Detail::RawMember::Get(const Object* target) const
    {
        std::uint64_t targetHandle = 0;
        if (const auto problem = TargetHandle(state->component, target, targetHandle))
        {
            return Failure<std::uint64_t>(problem->first, problem->second);
        }
        ManagedDiagnosticBuffer diagnostic;
        std::uint64_t value = 0;
        const auto status =
            state->bridge->getMember(state->component, state->member, targetHandle, &value, diagnostic.bytes.data(),
                                     diagnostic.Capacity, &diagnostic.written, &diagnostic.truncated);
        try
        {
            return CompleteCall<std::uint64_t>(status, "GetMember", diagnostic, value);
        }
        catch (...)
        {
            // The block is not owned by anything yet; give it back rather than leak it.
            if (state->returnsBlock && status == 0 && value != 0)
            {
                state->bridge->freeString(value);
            }
            throw;
        }
    }

    CallResult<void> Detail::RawMember::Set(const Object* target, std::uint64_t value, const Object* objectValue) const
    {
        std::uint64_t targetHandle = 0;
        if (const auto problem = TargetHandle(state->component, target, targetHandle))
        {
            return Failure<void>(problem->first, problem->second);
        }
        if (objectValue != nullptr)
        {
            std::uint64_t slot = 0;
            const Object* objects[1] = {objectValue};
            if (const auto problem =
                    RawMethod::ResolveObjects(state->component, std::span<std::uint64_t>(&slot, 1), objects))
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
