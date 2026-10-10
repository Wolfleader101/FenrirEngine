#include "FenrirDotNetHost/ManagedComponent.hpp"

#include "ManagedComponentState.hpp"
#include "ManagedDiagnosticBuffer.hpp"
#include "Utf8Buffer.hpp"

#include <limits>
#include <utility>

namespace Fenrir::Scripting
{
    ManagedComponent::State::~State() noexcept
    {
        const auto observedToken = token != 0 ? token : trackingToken;
        std::int32_t written = 0, truncated = 0;
        if (token != 0)
        {
            bridge->release(token, nullptr, 0, &written, &truncated);
        }
        if (observedToken != 0)
            bridge->forgetUnload(observedToken, nullptr, 0, &written, &truncated);
    }

    ManagedComponent::ManagedComponent(std::unique_ptr<State> ownerState) noexcept : state(std::move(ownerState))
    {
    }
    ManagedComponent::~ManagedComponent() noexcept = default;
    ManagedComponent::ManagedComponent(ManagedComponent&&) noexcept = default;
    ManagedComponent& ManagedComponent::operator=(ManagedComponent&&) noexcept = default;

    bool ManagedComponent::IsLoaded() const noexcept
    {
        return state != nullptr && state->token != 0;
    }

    ComponentInfoResult ManagedComponent::GetInfo() const
    {
        if (!IsLoaded())
        {
            return {ManagedCallStatus::InvalidHandle, {}, "The component owner has been released or moved."};
        }
        const auto name = Detail::ReadUtf8Buffer(
            [this](std::uint8_t* output, std::int32_t capacity, std::int32_t* required, std::uint8_t* diagnostic,
                   std::int32_t diagnosticCapacity, std::int32_t* written, std::int32_t* truncated) {
                return state->bridge->copyName(state->token, output, capacity, required, diagnostic, diagnosticCapacity,
                                               written, truncated);
            });
        if (name.status != ManagedCallStatus::Success)
        {
            return {name.status, {}, name.diagnostic, name.diagnosticTruncated};
        }
        return {ManagedCallStatus::Success, ComponentInfo{*name.value, state->isCollectible}, {}};
    }

    Int32CallResult ManagedComponent::InvokeInt32(std::string_view typeName, std::string_view methodName,
                                                  std::int32_t left, std::int32_t right) const
    {
        if (!IsLoaded())
            return {ManagedCallStatus::InvalidHandle, {}, "The component owner has been released or moved."};
        const auto maxLength = static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max());
        if (typeName.empty() || methodName.empty() || typeName.size() > maxLength || methodName.size() > maxLength)
            return {
                ManagedCallStatus::InvalidArgument, {}, "Type and method names must contain 1..INT32_MAX UTF-8 bytes."};
        if (typeName.find('\0') != std::string_view::npos || methodName.find('\0') != std::string_view::npos)
            return {ManagedCallStatus::InvalidArgument, {}, "Type and method names cannot contain null characters."};

        Detail::ManagedDiagnosticBuffer diagnostic;
        std::int32_t value = 0;
        const auto status = state->bridge->invokeInt32(
            state->token, reinterpret_cast<const std::uint8_t*>(typeName.data()),
            static_cast<std::int32_t>(typeName.size()), reinterpret_cast<const std::uint8_t*>(methodName.data()),
            static_cast<std::int32_t>(methodName.size()), left, right, &value, diagnostic.bytes.data(),
            diagnostic.Capacity, &diagnostic.written, &diagnostic.truncated);
        Int32CallResult result;
        switch (static_cast<ManagedCallStatus>(status))
        {
        case ManagedCallStatus::Success:
            return diagnostic.ApplyTo(Int32CallResult{ManagedCallStatus::Success, value, {}});
        case ManagedCallStatus::InvalidArgument:
            result = {ManagedCallStatus::InvalidArgument, {}, "The component invocation rejected an argument."};
            break;
        case ManagedCallStatus::Overflow:
            result = {ManagedCallStatus::Overflow, {}, "The component calculation overflowed a 32-bit integer."};
            break;
        case ManagedCallStatus::ManagedException:
            result = {ManagedCallStatus::ManagedException, {}, "Component lookup or invocation caught an exception."};
            break;
        case ManagedCallStatus::InvalidHandle:
            result = {ManagedCallStatus::InvalidHandle, {}, "The component token is invalid."};
            break;
        default:
            return {ManagedCallStatus::UnexpectedStatus,
                    {},
                    "Component invocation returned an unknown status: " + std::to_string(status)};
        }
        return diagnostic.ApplyTo(std::move(result));
    }

    ComponentUnloadResult ManagedComponent::GetUnloadStatus() const
    {
        if (state == nullptr || (state->token == 0 && state->trackingToken == 0))
            return {ManagedCallStatus::InvalidHandle, {}, "The component owner has been moved."};
        const bool active = IsLoaded();
        const auto token = active ? state->token : state->trackingToken;
        Detail::ManagedDiagnosticBuffer diagnostic;
        std::int32_t observed = -1;
        const auto status = state->bridge->getUnloadState(
            token, &observed, diagnostic.bytes.data(), diagnostic.Capacity, &diagnostic.written, &diagnostic.truncated);
        ComponentUnloadResult result;
        switch (static_cast<ManagedCallStatus>(status))
        {
        case ManagedCallStatus::Success:
            if ((active && observed != 0) || (!active && observed != 1 && observed != 2))
                return {ManagedCallStatus::InvalidResponse,
                        {},
                        "The unload query returned an invalid state for this owner."};
            return diagnostic.ApplyTo(
                ComponentUnloadResult{ManagedCallStatus::Success, static_cast<ComponentUnloadState>(observed), {}});
        case ManagedCallStatus::InvalidHandle:
            result = {ManagedCallStatus::InvalidHandle, {}, "The unload tracking token is invalid."};
            break;
        case ManagedCallStatus::ManagedException:
            result = {ManagedCallStatus::ManagedException, {}, "The unload query caught an exception."};
            break;
        default:
            return {ManagedCallStatus::UnexpectedStatus,
                    {},
                    "Unload query returned an unknown status: " + std::to_string(status)};
        }
        return diagnostic.ApplyTo(std::move(result));
    }

    ComponentReleaseResult ManagedComponent::Release()
    {
        if (!IsLoaded())
            return {ManagedCallStatus::Success, {}};
        const auto token = std::exchange(state->token, 0);
        state->trackingToken = token;
        Detail::ManagedDiagnosticBuffer diagnostic;
        const auto status = state->bridge->release(token, diagnostic.bytes.data(), diagnostic.Capacity,
                                                   &diagnostic.written, &diagnostic.truncated);
        Utf8StringResult result;
        switch (static_cast<ManagedCallStatus>(status))
        {
        case ManagedCallStatus::Success:
            result = {ManagedCallStatus::Success, {}, {}};
            break;
        case ManagedCallStatus::InvalidHandle:
            result = {ManagedCallStatus::InvalidHandle, {}, "The component token is invalid."};
            break;
        case ManagedCallStatus::ManagedException:
            result = {
                ManagedCallStatus::ManagedException, {}, "Component unload request failed after registration removal."};
            break;
        default:
            result = {ManagedCallStatus::UnexpectedStatus,
                      {},
                      "Component release returned an unexpected status: " + std::to_string(status)};
            break;
        }
        result = diagnostic.ApplyTo(std::move(result));
        return {result.status, std::move(result.diagnostic), result.diagnosticTruncated};
    }
} // namespace Fenrir::Scripting
