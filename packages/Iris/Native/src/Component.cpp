#include "Iris/Component.hpp"

#include "ComponentState.hpp"
#include "ManagedDiagnosticBuffer.hpp"
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
            bridge->release(token, nullptr, 0, &written, &truncated);
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

    CallResult<std::int32_t> Component::InvokeInt32(std::string_view typeName, std::string_view methodName,
                                                    std::int32_t left, std::int32_t right) const
    {
        if (!IsLoaded())
        {
            return Detail::Failure<std::int32_t>(CallStatus::InvalidHandle, ReleasedOwner);
        }
        const auto maxLength = static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max());
        if (typeName.empty() || methodName.empty() || typeName.size() > maxLength || methodName.size() > maxLength)
        {
            return Detail::Failure<std::int32_t>(CallStatus::InvalidArgument,
                                                 "Type and method names must contain 1..INT32_MAX UTF-8 bytes.");
        }
        if (typeName.find('\0') != std::string_view::npos || methodName.find('\0') != std::string_view::npos)
        {
            return Detail::Failure<std::int32_t>(CallStatus::InvalidArgument,
                                                 "Type and method names cannot contain null characters.");
        }

        Detail::ManagedDiagnosticBuffer diagnostic;
        std::int32_t value = 0;
        const auto status = state->bridge->invokeInt32(
            state->token, reinterpret_cast<const std::uint8_t*>(typeName.data()),
            static_cast<std::int32_t>(typeName.size()), reinterpret_cast<const std::uint8_t*>(methodName.data()),
            static_cast<std::int32_t>(methodName.size()), left, right, &value, diagnostic.bytes.data(),
            diagnostic.Capacity, &diagnostic.written, &diagnostic.truncated);
        return Detail::CompleteCall<std::int32_t>(status, "InvokeComponentInt32", diagnostic, value);
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
        // Invalidate this owner first; it keeps the token only to observe unloading.
        const auto token = std::exchange(state->token, 0);
        state->trackingToken = token;
        Detail::ManagedDiagnosticBuffer diagnostic;
        const auto status = state->bridge->release(token, diagnostic.bytes.data(), diagnostic.Capacity,
                                                   &diagnostic.written, &diagnostic.truncated);
        return Detail::CompleteCall<void>(status, "ReleaseComponent", diagnostic);
    }
} // namespace Iris
