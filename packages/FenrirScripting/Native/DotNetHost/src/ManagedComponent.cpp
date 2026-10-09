#include "FenrirDotNetHost/ManagedComponent.hpp"

#include "ManagedComponentState.hpp"
#include "ManagedDiagnosticBuffer.hpp"
#include "Utf8Buffer.hpp"

#include <utility>

namespace Fenrir::Scripting
{
    ManagedComponent::State::~State() noexcept
    {
        if (token != 0)
        {
            std::int32_t written = 0, truncated = 0;
            bridge->release(token, nullptr, 0, &written, &truncated);
        }
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

    ComponentReleaseResult ManagedComponent::Release()
    {
        if (!IsLoaded())
            return {ManagedCallStatus::Success, {}};
        const auto token = std::exchange(state->token, 0);
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
