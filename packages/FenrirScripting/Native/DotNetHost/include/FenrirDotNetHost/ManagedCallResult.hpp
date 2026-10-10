#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace Fenrir::Scripting
{
    enum class ManagedCallStatus : std::int32_t
    {
        // 0..5 are the managed bridge's wire statuses (BridgeStatus); negative values are native-only.
        Success = 0,
        InvalidArgument = 1,
        Overflow = 2,
        ManagedException = 3,
        BufferTooSmall = 4,
        InvalidHandle = 5,
        NotInitialized = -1,
        UnexpectedStatus = -2,
        InvalidResponse = -3
    };

    /**
     * @brief The outcome of one call into managed code.
     *
     * `value` is present only on Success. `diagnostic` owns UTF-8 text: the managed exception's
     * ToString(), at most 4096 bytes and cut on a scalar boundary (`diagnosticTruncated`), or a
     * native fallback when managed details are missing or invalid. A call is never repeated to
     * produce its diagnostic. Results reference no host or owner storage.
     */
    template <typename T>
    struct ManagedCallResult
    {
        ManagedCallStatus status = ManagedCallStatus::NotInitialized;
        std::optional<T> value;
        std::string diagnostic;
        bool diagnosticTruncated = false;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return status == ManagedCallStatus::Success;
        }
    };

    template <>
    struct ManagedCallResult<void>
    {
        ManagedCallStatus status = ManagedCallStatus::NotInitialized;
        std::string diagnostic;
        bool diagnosticTruncated = false;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return status == ManagedCallStatus::Success;
        }
    };
} // namespace Fenrir::Scripting
