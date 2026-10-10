#pragma once

#include "FenrirDotNetHost/ManagedCallResult.hpp"
#include "StringEncoding.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace Fenrir::Scripting::Detail
{
    // Diagnostic storage for one bridge call. Managed code borrows it for the call only.
    struct ManagedDiagnosticBuffer
    {
        static constexpr std::int32_t Capacity = 4096;
        std::array<std::uint8_t, Capacity> bytes{};
        std::int32_t written = 0;
        std::int32_t truncated = 0;

        // Copies valid managed details over the result's fallback text. Success must carry no
        // metadata; invalid metadata keeps the fallback and says why.
        template <typename T>
        [[nodiscard]] ManagedCallResult<T> ApplyTo(ManagedCallResult<T> result) const
        {
            if (result.status == ManagedCallStatus::Success)
            {
                if (written != 0 || truncated != 0)
                {
                    ManagedCallResult<T> invalid;
                    invalid.status = ManagedCallStatus::InvalidResponse;
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
    [[nodiscard]] ManagedCallResult<T> Failure(ManagedCallStatus status, std::string diagnostic)
    {
        ManagedCallResult<T> result;
        result.status = status;
        result.diagnostic = std::move(diagnostic);
        return result;
    }

    // Carries a failure's status and diagnostic over to a result of another type.
    template <typename T, typename U>
    [[nodiscard]] ManagedCallResult<T> Failure(const ManagedCallResult<U>& failed)
    {
        auto result = Failure<T>(failed.status, failed.diagnostic);
        result.diagnosticTruncated = failed.diagnosticTruncated;
        return result;
    }

    // Builds the result of one bridge call from its wire status. Success carries `value`; a bridge
    // failure carries the managed diagnostic or a fallback naming `operation`; any other status
    // becomes UnexpectedStatus.
    template <typename T, typename... Value>
    [[nodiscard]] ManagedCallResult<T> CompleteCall(std::int32_t status, const char* operation,
                                                    const ManagedDiagnosticBuffer& diagnostic, Value&&... value)
    {
        const std::string name(operation);
        switch (static_cast<ManagedCallStatus>(status))
        {
        case ManagedCallStatus::Success: {
            ManagedCallResult<T> result;
            result.status = ManagedCallStatus::Success;
            if constexpr (!std::is_void_v<T>)
            {
                result.value.emplace(std::forward<Value>(value)...);
            }
            return diagnostic.ApplyTo(std::move(result));
        }
        case ManagedCallStatus::InvalidArgument:
            return diagnostic.ApplyTo(Failure<T>(ManagedCallStatus::InvalidArgument, name + " rejected an argument."));
        case ManagedCallStatus::Overflow:
            return diagnostic.ApplyTo(Failure<T>(ManagedCallStatus::Overflow, name + " overflowed."));
        case ManagedCallStatus::ManagedException:
            return diagnostic.ApplyTo(
                Failure<T>(ManagedCallStatus::ManagedException, name + " caught a managed exception."));
        case ManagedCallStatus::BufferTooSmall:
            return diagnostic.ApplyTo(Failure<T>(ManagedCallStatus::BufferTooSmall, name + " needs a larger buffer."));
        case ManagedCallStatus::InvalidHandle:
            return diagnostic.ApplyTo(
                Failure<T>(ManagedCallStatus::InvalidHandle, name + " received an invalid or released handle."));
        default:
            return Failure<T>(ManagedCallStatus::UnexpectedStatus,
                              name + " returned an unknown status: " + std::to_string(status) + '.');
        }
    }
} // namespace Fenrir::Scripting::Detail
