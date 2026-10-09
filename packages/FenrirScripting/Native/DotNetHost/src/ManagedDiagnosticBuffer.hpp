#pragma once

#include "FenrirDotNetHost/DotNetHost.hpp"
#include "StringEncoding.hpp"

#include <array>
#include <stdexcept>

namespace Fenrir::Scripting::Detail
{
    // Stack-owned for one invocation. Shared by the integer and UTF-8 adapters;
    // managed code borrows the buffer and metadata, never retaining any pointer.
    struct ManagedDiagnosticBuffer
    {
        static constexpr std::int32_t Capacity = 4096;
        std::array<std::uint8_t, Capacity> bytes{};
        std::int32_t written = 0;
        std::int32_t truncated = 0;

        template <typename Result>
        [[nodiscard]] Result ApplyTo(Result result) const
        {
            if (result.status == ManagedCallStatus::Success)
            {
                if (written != 0 || truncated != 0)
                {
                    return {ManagedCallStatus::InvalidResponse,
                            {},
                            "A successful managed call returned diagnostic metadata."};
                }
                return result;
            }
            if (result.status == ManagedCallStatus::UnexpectedStatus)
            {
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
                // Strict validation; allocation/system errors retain native behavior.
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
} // namespace Fenrir::Scripting::Detail
