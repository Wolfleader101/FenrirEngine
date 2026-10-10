#pragma once

#include "ManagedDiagnosticBuffer.hpp"

#include <string>

namespace Fenrir::Scripting::Detail
{
    // Reads a managed UTF-8 payload in two calls: a size query (null destination, zero capacity),
    // then a copy into native storage of exactly that size. `copy` takes the payload buffer,
    // capacity and byte count, then the diagnostic buffer, capacity, written count and truncation
    // flag. Each call gets its own diagnostics. A failed query never copies, a failed copy is never
    // retried, and only success exposes the string. Allocation may throw.
    template <typename Copy>
    [[nodiscard]] ManagedCallResult<std::string> ReadUtf8Buffer(const char* operation, Copy&& copy)
    {
        std::int32_t requiredBytes = 0;
        ManagedDiagnosticBuffer queryDiagnostic;
        const auto queried =
            CompleteCall<void>(copy(nullptr, 0, &requiredBytes, queryDiagnostic.bytes.data(), queryDiagnostic.Capacity,
                                    &queryDiagnostic.written, &queryDiagnostic.truncated),
                               operation, queryDiagnostic);
        if (!queried.Succeeded())
        {
            return Failure<std::string>(queried);
        }
        if (requiredBytes < 0)
        {
            return Failure<std::string>(ManagedCallStatus::InvalidResponse,
                                        std::string(operation) + " returned a negative byte count.");
        }

        std::string value(static_cast<std::size_t>(requiredBytes), '\0');
        std::int32_t copiedBytes = 0;
        ManagedDiagnosticBuffer copyDiagnostic;
        const auto copied = CompleteCall<void>(copy(reinterpret_cast<std::uint8_t*>(value.data()), requiredBytes,
                                                    &copiedBytes, copyDiagnostic.bytes.data(), copyDiagnostic.Capacity,
                                                    &copyDiagnostic.written, &copyDiagnostic.truncated),
                                               operation, copyDiagnostic);
        if (!copied.Succeeded())
        {
            return Failure<std::string>(copied);
        }
        if (copiedBytes != requiredBytes)
        {
            return Failure<std::string>(ManagedCallStatus::InvalidResponse,
                                        std::string(operation) + " changed its byte count during the copy.");
        }
        ManagedCallResult<std::string> result;
        result.status = ManagedCallStatus::Success;
        result.value = std::move(value);
        return result;
    }
} // namespace Fenrir::Scripting::Detail
