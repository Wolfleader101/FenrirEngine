#include "Iris/Component.hpp"

#include "ComponentState.hpp"
#include "ManagedDiagnosticBuffer.hpp"
#include "Names.hpp"
#include "TypeState.hpp"
#include "Utf8Buffer.hpp"

#include <limits>
#include <utility>

namespace Iris
{
    namespace
    {
        constexpr const char* ReleasedOwner = "The component owner has been released or moved.";
    } // namespace

    Component::State::~State() noexcept
    {
        // Best effort: release a live registration, then drop the weak unload observation.
        const auto observedToken = token != 0 ? token : trackingToken;
        std::int32_t written = 0;
        std::int32_t truncated = 0;
        if (token != 0)
        {
            // Destroying an owner always releases, even with live objects, so a leaked object can
            // never pin the registration. Explicit Release is the call that refuses.
            bridge->forceRelease(token, nullptr, 0, &written, &truncated);
        }
        if (observedToken != 0)
        {
            bridge->forgetUnload(observedToken, nullptr, 0, &written, &truncated);
        }
    }

    Component::Component(std::unique_ptr<State> ownerState) noexcept : state(std::move(ownerState))
    {
    }
    Component::~Component() noexcept = default;
    Component::Component(Component&&) noexcept = default;
    Component& Component::operator=(Component&&) noexcept = default;

    bool Component::IsLoaded() const noexcept
    {
        return state != nullptr && state->token != 0;
    }

    CallResult<ComponentInfo> Component::GetInfo() const
    {
        if (!IsLoaded())
        {
            return Detail::Failure<ComponentInfo>(CallStatus::InvalidHandle, ReleasedOwner);
        }
        auto name = Detail::ReadUtf8Buffer(
            "CopyComponentNameUtf8",
            [this](std::uint8_t* output, std::int32_t capacity, std::int32_t* required, std::uint8_t* diagnostic,
                   std::int32_t diagnosticCapacity, std::int32_t* written, std::int32_t* truncated) {
                return state->bridge->copyName(state->token, output, capacity, required, diagnostic, diagnosticCapacity,
                                               written, truncated);
            });
        if (!name.Succeeded())
        {
            return Detail::PropagateFailure<ComponentInfo>(name);
        }
        return Detail::Success<ComponentInfo>(ComponentInfo{std::move(*name.value)});
    }

    CallResult<Type> Component::FindType(std::string_view fullName) const
    {
        if (!IsLoaded())
        {
            return Detail::Failure<Type>(CallStatus::InvalidHandle, ReleasedOwner);
        }
        if (const auto problem = Detail::NameProblem(fullName, "type"))
        {
            return Detail::Failure<Type>(CallStatus::InvalidArgument, *problem);
        }
        Detail::ManagedDiagnosticBuffer diagnostic;
        std::uint64_t typeId = 0;
        const auto status =
            state->bridge->findType(state->token, reinterpret_cast<const std::uint8_t*>(fullName.data()),
                                    static_cast<std::int32_t>(fullName.size()), &typeId, diagnostic.bytes.data(),
                                    diagnostic.Capacity, &diagnostic.written, &diagnostic.truncated);
        if (static_cast<CallStatus>(status) == CallStatus::Success && typeId == 0)
        {
            return Detail::Failure<Type>(CallStatus::InvalidResponse, "FindType returned the invalid handle 0.");
        }
        auto handle = std::make_shared<Type::State>();
        handle->bridge = state->bridge;
        handle->component = state->token;
        handle->type = typeId;
        return Detail::CompleteCall<Type>(status, "FindType", diagnostic, Type(std::move(handle)));
    }

    CallResult<ComponentUnloadState> Component::GetUnloadStatus() const
    {
        if (state == nullptr || (state->token == 0 && state->trackingToken == 0))
        {
            return Detail::Failure<ComponentUnloadState>(CallStatus::InvalidHandle,
                                                         "The component owner has been moved.");
        }
        const bool loaded = IsLoaded();
        Detail::ManagedDiagnosticBuffer diagnostic;
        std::int32_t observed = -1;
        const auto status = state->bridge->getUnloadState(loaded ? state->token : state->trackingToken, &observed,
                                                          diagnostic.bytes.data(), diagnostic.Capacity,
                                                          &diagnostic.written, &diagnostic.truncated);
        // A loaded owner can only be NotRequested; a released one only Pending or Collected.
        const auto observedState = static_cast<ComponentUnloadState>(observed);
        const bool consistent = loaded ? observedState == ComponentUnloadState::NotRequested
                                       : (observedState == ComponentUnloadState::Pending ||
                                          observedState == ComponentUnloadState::Collected);
        if (static_cast<CallStatus>(status) == CallStatus::Success && !consistent)
        {
            return Detail::Failure<ComponentUnloadState>(CallStatus::InvalidResponse,
                                                         "The unload query returned an invalid state for this owner.");
        }
        return Detail::CompleteCall<ComponentUnloadState>(status, "GetComponentUnloadState", diagnostic, observedState);
    }

    CallResult<void> Component::Release()
    {
        if (!IsLoaded())
        {
            return Detail::Success<void>();
        }
        Detail::ManagedDiagnosticBuffer diagnostic;
        const auto status = state->bridge->release(state->token, diagnostic.bytes.data(), diagnostic.Capacity,
                                                   &diagnostic.written, &diagnostic.truncated);
        auto result = Detail::CompleteCall<void>(status, "ReleaseComponent", diagnostic);
        // A refused release (live objects) leaves the component loaded and usable. Any other outcome
        // invalidates this owner, which keeps the token only to observe unloading.
        if (result.status != CallStatus::InUse)
        {
            state->trackingToken = std::exchange(state->token, 0);
        }
        return result;
    }
} // namespace Iris
