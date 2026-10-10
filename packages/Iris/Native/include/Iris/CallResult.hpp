#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace Iris
{
    enum class CallStatus : std::int32_t
    {
        // 0..7 are the managed bridge's wire statuses (BridgeStatus; 2 is unused); negative values are native-only.
        Success = 0,
        InvalidArgument = 1,
        ManagedException = 3,
        BufferTooSmall = 4,
        InvalidHandle = 5,
        NotFound = 6,
        InUse = 7,
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
    struct CallResult
    {
        CallStatus status = CallStatus::NotInitialized;
        std::optional<T> value;
        std::string diagnostic;
        bool diagnosticTruncated = false;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return status == CallStatus::Success;
        }
    };

    template <>
    struct CallResult<void>
    {
        CallStatus status = CallStatus::NotInitialized;
        std::string diagnostic;
        bool diagnosticTruncated = false;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return status == CallStatus::Success;
        }
    };
} // namespace Iris
