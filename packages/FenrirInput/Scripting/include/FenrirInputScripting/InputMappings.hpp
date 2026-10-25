#pragma once

// FenrirInput's ids as wrappers: a native function takes and returns a WindowId or GamepadId by value, crossing as its
// one 32-bit value (ADR-0044). Everything else C# sees of FenrirInput is generated
// (Scripting/api/FenrirInput.binding.toml) into FenrirInputScripting/Generated/FenrirInputMappings.hpp, which includes
// this header; another package's binding that uses FenrirInput's (FenrirWindow's) includes that header, and so these.

#include <Iris/InternalCall.hpp>

#include <cstdint>

#include "FenrirInput/InputTypes.hpp"

template <>
struct Iris::WrapperOf<Fenrir::WindowId>
{
    using Value = std::uint32_t;

    static Fenrir::WindowId From(Value value)
    {
        return Fenrir::WindowId{value};
    }

    static Value To(Fenrir::WindowId window)
    {
        return window.value;
    }
};

template <>
struct Iris::WrapperOf<Fenrir::GamepadId>
{
    using Value = std::uint32_t;

    static Fenrir::GamepadId From(Value value)
    {
        return Fenrir::GamepadId{value};
    }

    static Value To(Fenrir::GamepadId gamepad)
    {
        return gamepad.value;
    }
};
