#pragma once

// The native functions the example component's Natives class calls. A host registers them once, before or
// after Host::Initialize, and the component then loads; without them the load fails with a diagnostic.
#include "Iris/InternalCall.hpp"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <stdexcept>

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

    // Registers every function; returns the first failure, or a success.
    [[nodiscard]] inline Iris::CallResult<void> RegisterNatives()
    {
        const auto prefix = std::string("Iris.Examples.Component.Natives.");
        Iris::CallResult<void> results[] = {
            Iris::RegisterInternalCall<&Add>(prefix + "Add"),
            Iris::RegisterInternalCall<&Half>(prefix + "Half"),
            Iris::RegisterInternalCall<&Combine>(prefix + "Combine"),
            Iris::RegisterInternalCall<&Measure>(prefix + "Measure"),
            Iris::RegisterInternalCall<&Normalize>(prefix + "Normalize"),
            Iris::RegisterInternalCall<&Throws>(prefix + "Throws"),
            Iris::RegisterInternalCall<&Record>(prefix + "Record"),
            Iris::RegisterInternalCall<&Negate>(prefix + "Negate"),
            Iris::RegisterInternalCall<&NextChar>(prefix + "NextChar"),
            Iris::RegisterInternalCall<&LeakStopped>(prefix + "LeakStopped"),
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
