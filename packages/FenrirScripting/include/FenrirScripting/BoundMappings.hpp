#pragma once

// The hand-written mappings of the engine's glm types, the bound types of the math binding
// (api/FenrirMath.binding.toml, [bound-types]) and of every binding that holds them: glm's vectors, quaternion and
// matrix, as Fenrir.Math's Vector2, Vector3, Vector4, Quaternion and Matrix4x4 (ADR-0029). A binding maps its bound
// types by hand; the rest of FenrirMath (Colour, Ray, Bounds) is generated, into
// FenrirScripting/Generated/FenrirMathMappings.hpp, which includes this header, as every binding's generated mappings
// do. A mapping a registration uses is checked against its C# struct whenever a script assembly loads.

#include <Iris/Iris.hpp>

#include <cstddef>
#include <string_view>

#include "FenrirMath/Math.hpp"

template <>
struct Iris::StructMapping<Fenrir::Math::Vec3>
{
    static constexpr std::string_view typeName = "Fenrir.Math.Vector3";
    static constexpr auto fields = Iris::StructFields(Iris::Field<float>("X", offsetof(Fenrir::Math::Vec3, x)),
                                                      Iris::Field<float>("Y", offsetof(Fenrir::Math::Vec3, y)),
                                                      Iris::Field<float>("Z", offsetof(Fenrir::Math::Vec3, z)));
};

// glm keeps w first in memory, and the C# Quaternion declares its fields in that order (fields are listed in
// declaration order, and each offset is checked).
template <>
struct Iris::StructMapping<Fenrir::Math::Quat>
{
    static constexpr std::string_view typeName = "Fenrir.Math.Quaternion";
    static constexpr auto fields = Iris::StructFields(Iris::Field<float>("W", offsetof(Fenrir::Math::Quat, w)),
                                                      Iris::Field<float>("X", offsetof(Fenrir::Math::Quat, x)),
                                                      Iris::Field<float>("Y", offsetof(Fenrir::Math::Quat, y)),
                                                      Iris::Field<float>("Z", offsetof(Fenrir::Math::Quat, z)));
};

template <>
struct Iris::StructMapping<glm::vec2>
{
    static constexpr std::string_view typeName = "Fenrir.Math.Vector2";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(glm::vec2, x, "X"), IRIS_FIELD_AS(glm::vec2, y, "Y"));
};

template <>
struct Iris::StructMapping<glm::vec4>
{
    static constexpr std::string_view typeName = "Fenrir.Math.Vector4";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(glm::vec4, x, "X"), IRIS_FIELD_AS(glm::vec4, y, "Y"),
                           IRIS_FIELD_AS(glm::vec4, z, "Z"), IRIS_FIELD_AS(glm::vec4, w, "W"));
};

// glm::mat4 is four column vectors, one after another, but keeps them private, so its mapping gives each by offset
// rather than naming it.
static_assert(sizeof(Fenrir::Math::Mat4) == 4 * sizeof(glm::vec4) && alignof(Fenrir::Math::Mat4) == alignof(glm::vec4),
              "glm::mat4 is no longer four packed columns: update its mapping and the C# Matrix4x4.");

template <>
struct Iris::StructMapping<Fenrir::Math::Mat4>
{
    static constexpr std::string_view typeName = "Fenrir.Math.Matrix4x4";
    static constexpr auto fields =
        Iris::StructFields(Iris::Field<glm::vec4>("Column0", 0), Iris::Field<glm::vec4>("Column1", sizeof(glm::vec4)),
                           Iris::Field<glm::vec4>("Column2", 2 * sizeof(glm::vec4)),
                           Iris::Field<glm::vec4>("Column3", 3 * sizeof(glm::vec4)));
};
