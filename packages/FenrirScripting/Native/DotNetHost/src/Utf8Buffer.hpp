#pragma once

#include "FenrirDotNetHost/DotNetHost.hpp"
#include "ManagedDiagnosticBuffer.hpp"

#include <utility>

namespace Fenrir::Scripting::Detail
{
    // Both adapters take a payload buffer/count, followed by diagnostic buffer,
    // capacity, bytes-written and truncation outputs. Fresh diagnostics per invocation.
    // A null destination with zero capacity queries the size. The payload must remain
    // stable between query and copy. The adapter borrows these buffers only during calls.
    // No retries or partial output are exposed. Allocations may throw C++ exceptions.
    template <typename Copy>
    [[nodiscard]] Utf8StringResult ReadUtf8Buffer(Copy&& copy)
    {
        const auto failure = [](std::int32_t status) -> Utf8StringResult {
            switch (static_cast<ManagedCallStatus>(status))
            {
            case ManagedCallStatus::InvalidArgument:
                return {ManagedCallStatus::InvalidArgument,
                        {},
                        "The managed UTF-8 call rejected an argument or invalid UTF-8."};
            case ManagedCallStatus::ManagedException:
                return {ManagedCallStatus::ManagedException, {}, "The managed UTF-8 call caught an exception."};
            case ManagedCallStatus::BufferTooSmall:
                return {ManagedCallStatus::BufferTooSmall, {}, "The managed UTF-8 call requires a larger buffer."};
            case ManagedCallStatus::InvalidHandle:
                return {ManagedCallStatus::InvalidHandle, {}, "The managed component token is invalid."};
            default:
                return {ManagedCallStatus::UnexpectedStatus,
                        {},
                        "The managed UTF-8 call returned an unexpected status: " + std::to_string(status) + '.'};
            }
        };

        std::int32_t requiredBytes = 0;
        ManagedDiagnosticBuffer queryDiagnostic;
        const std::int32_t queryStatus =
            copy(nullptr, 0, &requiredBytes, queryDiagnostic.bytes.data(), queryDiagnostic.Capacity,
                 &queryDiagnostic.written, &queryDiagnostic.truncated);
        const auto queryResult = queryDiagnostic.ApplyTo(
            queryStatus == 0 ? Utf8StringResult{ManagedCallStatus::Success, {}, {}} : failure(queryStatus));
        if (queryResult.status != ManagedCallStatus::Success)
        {
            return queryResult;
        }
        if (requiredBytes < 0)
        {
            return {ManagedCallStatus::InvalidResponse, {}, "The managed UTF-8 call returned a negative byte count."};
        }

        std::string value(static_cast<std::size_t>(requiredBytes), '\0');
        std::int32_t copiedBytes = 0;
        ManagedDiagnosticBuffer copyDiagnostic;
        const std::int32_t copyStatus = copy(reinterpret_cast<std::uint8_t*>(value.data()), requiredBytes, &copiedBytes,
                                             copyDiagnostic.bytes.data(), copyDiagnostic.Capacity,
                                             &copyDiagnostic.written, &copyDiagnostic.truncated);
        const auto copyResult = copyDiagnostic.ApplyTo(
            copyStatus == 0 ? Utf8StringResult{ManagedCallStatus::Success, {}, {}} : failure(copyStatus));
        if (copyResult.status != ManagedCallStatus::Success)
        {
            return copyResult;
        }
        if (copiedBytes != requiredBytes)
        {
            return {ManagedCallStatus::InvalidResponse, {}, "The managed UTF-8 byte count changed during the copy."};
        }
        return {ManagedCallStatus::Success, std::move(value), {}};
    }
} // namespace Fenrir::Scripting::Detail
