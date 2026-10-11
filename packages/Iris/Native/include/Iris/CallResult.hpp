#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace Iris
{
    enum class CallStatus : std::int32_t
    {
        // 0..7 mirror the managed bridge's wire statuses (2 and 4 are not used publicly); negative values are
        // native-only.
        Success = 0,
        InvalidArgument = 1,
        ManagedException = 3,
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
     * `value` is present only on Success; `*result` and `result->` reach it like std::optional, with the same
     * precondition (check Succeeded() first). There is deliberately no conversion to bool: for a
     * `CallResult<bool>` it would test the call, not the answer. `diagnostic` owns UTF-8 text: the managed
     * exception's ToString(), at most 4096 bytes and cut on a scalar boundary (`diagnosticTruncated`), or a
     * native fallback when managed details are missing or invalid. A call is never repeated to produce its
     * diagnostic. Results reference no host or owner storage. A default-constructed result is NotInitialized.
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

        [[nodiscard]] T& operator*() & noexcept
        {
            return *value;
        }
        [[nodiscard]] const T& operator*() const& noexcept
        {
            return *value;
        }
        // Moves the value out: `Component component = *std::move(loaded);`.
        [[nodiscard]] T&& operator*() && noexcept
        {
            return std::move(*value);
        }
        [[nodiscard]] T* operator->() noexcept
        {
            return value.operator->();
        }
        [[nodiscard]] const T* operator->() const noexcept
        {
            return value.operator->();
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
