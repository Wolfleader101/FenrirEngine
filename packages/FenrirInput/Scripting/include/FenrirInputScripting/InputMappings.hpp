#pragma once

// How FenrirInput's types look to C#: each mapping names its mirror in the Fenrir.Input contract
// (Managed/Fenrir.Input), and is checked against it whenever a script assembly loads, as part of an event or resource
// that holds it, so a drift fails the load and names the field. Another package's extension whose events or resources
// hold a WindowId (FenrirWindow's) includes this header, so its mappings can name it as a field.

#include <Iris/Iris.hpp>

#include <string_view>

#include "FenrirInput/InputTypes.hpp"

template <>
struct Iris::StructMapping<Fenrir::WindowId>
{
    static constexpr std::string_view typeName = "Fenrir.Input.WindowId";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::WindowId, value, "Value"));
};

template <>
struct Iris::StructMapping<Fenrir::GamepadId>
{
    static constexpr std::string_view typeName = "Fenrir.Input.GamepadId";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::GamepadId, value, "Value"));
};

template <>
struct Iris::EnumMapping<Fenrir::Key>
{
    static constexpr std::string_view typeName = "Fenrir.Input.Key";
};

template <>
struct Iris::EnumMapping<Fenrir::Scancode>
{
    static constexpr std::string_view typeName = "Fenrir.Input.Scancode";
};

template <>
struct Iris::EnumMapping<Fenrir::KeyModifiers>
{
    static constexpr std::string_view typeName = "Fenrir.Input.KeyModifiers";
};

template <>
struct Iris::EnumMapping<Fenrir::MouseButton>
{
    static constexpr std::string_view typeName = "Fenrir.Input.MouseButton";
};

template <>
struct Iris::EnumMapping<Fenrir::GamepadButton>
{
    static constexpr std::string_view typeName = "Fenrir.Input.GamepadButton";
};

template <>
struct Iris::EnumMapping<Fenrir::GamepadAxis>
{
    static constexpr std::string_view typeName = "Fenrir.Input.GamepadAxis";
};
