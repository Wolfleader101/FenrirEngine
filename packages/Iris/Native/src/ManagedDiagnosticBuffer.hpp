#pragma once

#include "Iris/CallResult.hpp"
#include "StringEncoding.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace Iris::Detail
{
    // Diagnostic storage for one bridge call. Managed code borrows it for the call only. The bytes are left
    // uninitialized on purpose: the bridge writes `written` bytes and only those are read, and zeroing 4 KiB
    // on every call (including every successful one) was a measurable part of a call's cost.
    struct ManagedDiagnosticBuffer
    {
        static constexpr std::int32_t Capacity = 4096;
        std::array<std::uint8_t, Capacity> bytes;
        std::int32_t written = 0;
        std::int32_t truncated = 0;

        // Copies valid managed details over the result's fallback text. Success must carry no
        // metadata; invalid metadata keeps the fallback and says why.
        template <typename T>
        [[nodiscard]] CallResult<T> ApplyTo(CallResult<T> result) const
        {
            if (result.status == CallStatus::Success)
            {
                if (written != 0 || truncated != 0)
                {
                    CallResult<T> invalid;
                    invalid.status = CallStatus::InvalidResponse;
                    invalid.diagnostic = "A successful managed call returned diagnostic metadata.";
                    return invalid;
                }
                return result;
            }
            if (written < 0 || written > Capacity || (truncated != 0 && truncated != 1))
            {
                result.diagnostic += " Managed diagnostic metadata was invalid.";
                return result;
            }
            const std::string_view text(reinterpret_cast<const char*>(bytes.data()), static_cast<std::size_t>(written));
            try
            {
                // Strict validation; allocation and system errors keep normal C++ behaviour.
                static_cast<void>(Utf8ToHostString(text));
            }
            catch (const std::invalid_argument&)
            {
                result.diagnostic += " Managed diagnostic text was not valid UTF-8.";
                return result;
            }
            if (!text.empty())
            {
                result.diagnostic.assign(text);
            }
            result.diagnosticTruncated = truncated == 1;
            return result;
        }
    };

    template <typename T>
    [[nodiscard]] CallResult<T> Failure(CallStatus status, std::string diagnostic)
    {
        CallResult<T> result;
        result.status = status;
        result.diagnostic = std::move(diagnostic);
        return result;
    }

    template <typename T, typename... Value>
    [[nodiscard]] CallResult<T> Success(Value&&... value)
    {
        CallResult<T> result;
        result.status = CallStatus::Success;
        if constexpr (!std::is_void_v<T>)
        {
            result.value.emplace(std::forward<Value>(value)...);
        }
        return result;
    }

    // Carries a failure's status and diagnostic over to a result of another type.
    template <typename T, typename U>
    [[nodiscard]] CallResult<T> PropagateFailure(const CallResult<U>& failed)
    {
        auto result = Failure<T>(failed.status, failed.diagnostic);
        result.diagnosticTruncated = failed.diagnosticTruncated;
        return result;
    }

    // The bridge's wire status 4 (BufferTooSmall). Native code always sizes buffers from the bridge's own
    // answer, so the bridge reporting it is a malformed response; it is not a public CallStatus.
    inline constexpr std::int32_t WireBufferTooSmall = 4;

    // Builds the result of one bridge call from its wire status. Success carries `value`. The bridge's
    // public statuses are passed through with the managed diagnostic, or a fallback naming `operation`;
    // BufferTooSmall becomes InvalidResponse, and any other status UnexpectedStatus without managed details.
    template <typename T, typename... Value>
    [[nodiscard]] CallResult<T> CompleteCall(std::int32_t status, const char* operation,
                                             const ManagedDiagnosticBuffer& diagnostic, Value&&... value)
    {
        const std::string name(operation);
        if (status == WireBufferTooSmall)
        {
            return diagnostic.ApplyTo(
                Failure<T>(CallStatus::InvalidResponse,
                           name + " reported a buffer too small, but the buffer was sized from its own answer."));
        }
        switch (static_cast<CallStatus>(status))
        {
        case CallStatus::Success:
            return diagnostic.ApplyTo(Success<T>(std::forward<Value>(value)...));
        case CallStatus::InvalidArgument:
            return diagnostic.ApplyTo(Failure<T>(CallStatus::InvalidArgument, name + " rejected an argument."));
        case CallStatus::ManagedException:
            return diagnostic.ApplyTo(Failure<T>(CallStatus::ManagedException, name + " caught a managed exception."));
        case CallStatus::InvalidHandle:
            return diagnostic.ApplyTo(
                Failure<T>(CallStatus::InvalidHandle, name + " received an invalid or released handle."));
        case CallStatus::NotFound:
            return diagnostic.ApplyTo(Failure<T>(CallStatus::NotFound, name + " found nothing."));
        case CallStatus::InUse:
            return diagnostic.ApplyTo(
                Failure<T>(CallStatus::InUse, name + " was refused because the component has live objects."));
        default:
            return Failure<T>(CallStatus::UnexpectedStatus,
                              name + " returned an unknown status: " + std::to_string(status) + '.');
        }
    }
} // namespace Iris::Detail
