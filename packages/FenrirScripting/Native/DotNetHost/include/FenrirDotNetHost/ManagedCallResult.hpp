#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace Fenrir::Scripting
{
    enum class ManagedCallStatus : std::int32_t
    {
        // 0..5 match the managed BridgeStatus wire contract.
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

    struct Int32CallResult
    {
        ManagedCallStatus status = ManagedCallStatus::NotInitialized;
        // Present only on Success; failure never exposes the output buffer.
        std::optional<std::int32_t> value;
        // Owning UTF-8 exception details, or a native fallback.
        std::string diagnostic;
        bool diagnosticTruncated = false;
    };

    struct Utf8StringResult
    {
        ManagedCallStatus status = ManagedCallStatus::NotInitialized;
        // Owning UTF-8 bytes, present only on Success. No added BOM or terminator.
        std::optional<std::string> value;
        std::string diagnostic;
        bool diagnosticTruncated = false;
    };
} // namespace Fenrir::Scripting
