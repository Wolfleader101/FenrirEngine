#pragma once

// The native functions the example component's Natives class calls. A host registers them once, before or
// after Host::Initialize, and the component then loads; without them the load fails with a diagnostic.
#include "Iris/InternalCall.hpp"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Iris::Examples
{
    inline std::atomic<int> recordedValue{0};

    inline int Add(int left, int right)
    {
        return left + right;
    }
    inline double Half(double value)
    {
        return value / 2;
    }
    inline long long Combine(long long left, std::uint8_t right)
    {
        return left * 1000 + right;
    }
    // A string arrives as UTF-8 bytes and a length; here the "work" is just reporting the byte count.
    inline int Measure(const char* text, int length)
    {
        return text == nullptr ? -1 : length;
    }
    // A struct arrives by pointer (here three floats).
    inline void Normalize(float* vector)
    {
        const float length = std::sqrt(vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2]);
        if (length > 0)
        {
            vector[0] /= length;
            vector[1] /= length;
            vector[2] /= length;
        }
    }
    // Throwing is allowed: Iris stops the exception at the boundary and returns zero to managed code.
    inline int Throws()
    {
        throw std::runtime_error("native failure");
    }
    inline void Record(int value)
    {
        recordedValue = value;
    }

    inline bool Negate(bool value)
    {
        return !value;
    }

    // The last text RecordText or StringBetweenNumbers received, so the checks can compare it with what was sent.
    inline std::mutex recordedTextMutex;
    inline std::string recordedText;
    inline void SetRecordedText(std::string_view text)
    {
        const std::lock_guard lock(recordedTextMutex);
        recordedText = std::string(text);
    }
    inline void ClearRecordedText()
    {
        SetRecordedText({});
    }
    inline std::string RecordedText()
    {
        const std::lock_guard lock(recordedTextMutex);
        return recordedText;
    }

    // A string parameter arrives as UTF-8 bytes in a std::string_view, valid for the call.
    inline std::int32_t RecordText(std::string_view text)
    {
        SetRecordedText(text);
        return static_cast<std::int32_t>(text.size());
    }
    inline std::int64_t StringBetweenNumbers(std::int32_t count, std::string_view label, std::int32_t factor)
    {
        SetRecordedText(label);
        return static_cast<std::int64_t>(count) * factor + static_cast<std::int64_t>(label.size());
    }

    // The unload checks' deliberate leak: a managed thread runs until the host sets this.
    inline std::atomic<bool> leakStop{false};
    inline bool LeakStopped()
    {
        return leakStop.load();
    }
    inline int Triple(int value)
    {
        return value * 3;
    }
    inline char16_t NextChar(char16_t value)
    {
        return static_cast<char16_t>(value + 1);
    }

    // An enum crosses by value as its underlying integer, which the managed field declares: an int for Gear, a byte
    // for Shade, as the managed enums of the same names have.
    enum class Gear : std::int32_t
    {
        Off = 0,
        On = 1,
        Auto = 7
    };
    enum class Shade : std::uint8_t
    {
        Dark = 1,
        Light = 2
    };
    inline Gear NextGear(Gear gear)
    {
        switch (gear)
        {
        case Gear::Off:
            return Gear::On;
        case Gear::On:
            return Gear::Auto;
        default:
            return Gear::Off;
        }
    }
    inline Shade Invert(Shade shade)
    {
        return shade == Shade::Dark ? Shade::Light : Shade::Dark;
    }

    // A wrapper crosses by value as its one field (see the Iris::WrapperOf specialization below); the managed field
    // declares a uint.
    struct Ticket
    {
        std::uint32_t number;
    };
    inline Ticket Advance(Ticket ticket, Gear by)
    {
        return Ticket{ticket.number + static_cast<std::uint32_t>(by)};
    }
} // namespace Iris::Examples

template <>
struct Iris::WrapperOf<Iris::Examples::Ticket>
{
    using Value = std::uint32_t;
    static Iris::Examples::Ticket From(Value number)
    {
        return Iris::Examples::Ticket{number};
    }
    static Value To(Iris::Examples::Ticket ticket)
    {
        return ticket.number;
    }
};

namespace Iris::Examples
{
    // Registers every function; returns the first failure, or a success.
    [[nodiscard]] inline Iris::CallResult<void> RegisterNatives()
    {
        const auto prefix = std::string("Iris.Examples.Component.Natives.");
        Iris::CallResult<void> results[] = {
            Iris::RegisterInternalCall<&Add>(prefix + "Add"),
            Iris::RegisterInternalCall<&Half>(prefix + "Half"),
            Iris::RegisterInternalCall<&Combine>(prefix + "Combine"),
            Iris::RegisterInternalCall<&Measure>(prefix + "Measure"),
            Iris::RegisterInternalCall<&RecordText>(prefix + "RecordText"),
            Iris::RegisterInternalCall<&StringBetweenNumbers>(prefix + "StringBetweenNumbers"),
            Iris::RegisterInternalCall<&Normalize>(prefix + "Normalize"),
            Iris::RegisterInternalCall<&Throws>(prefix + "Throws"),
            Iris::RegisterInternalCall<&Record>(prefix + "Record"),
            Iris::RegisterInternalCall<&Negate>(prefix + "Negate"),
            Iris::RegisterInternalCall<&NextChar>(prefix + "NextChar"),
            Iris::RegisterInternalCall<&LeakStopped>(prefix + "LeakStopped"),
            Iris::RegisterInternalCall<&NextGear>(prefix + "NextGear"),
            Iris::RegisterInternalCall<&Invert>(prefix + "Invert"),
            Iris::RegisterInternalCall<&Advance>(prefix + "Advance"),
            Iris::RegisterInternalCall<&Triple>("Iris.Examples.Contracts.SharedNatives.Triple"),
        };
        for (auto& result : results)
        {
            if (!result.Succeeded())
            {
                return result;
            }
        }
        return results[0];
    }
} // namespace Iris::Examples
