#pragma once

// The hand-written mappings of the engine's math types, which the Fenrir.Math contract mirrors (ADR-0029). Two are the
// built-in binding's bound types (api/FenrirScripting.binding.toml, [bound-types]): glm's vector and quaternion, as
// Fenrir.Math.Vector3 and Quaternion. A binding maps its bound types by hand; everything else the engine shares with C#
// is generated, into FenrirScripting/Generated/FenrirScriptingMappings.hpp, which includes this header.
//
// The rest are the math types no API description names yet, for the hand-written extensions whose components, events,
// resources and shared structs hold them: glm::vec2 and vec4 (Vector2, Vector4), glm::mat4 (Matrix4x4, through
// Detail::Mat4View), Fenrir::Colour (Colour), Math::Ray (Ray) and Math::AABB (Bounds, through Detail::BoundsView). A
// mapping a registration uses is checked against its C# struct whenever a script assembly loads.

#include <Iris/Iris.hpp>

#include <cstddef>
#include <string_view>

#include "FenrirMath/Colour.hpp"
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

namespace Fenrir::Detail
{
    // glm::mat4 is four column vectors, but keeps them private, so Iris cannot name them. A struct holding a matrix is
    // mapped through this stand-in, with the same layout, wherever a glm::mat4 is.
    struct Mat4View
    {
        glm::vec4 column0;
        glm::vec4 column1;
        glm::vec4 column2;
        glm::vec4 column3;
    };

    static_assert(sizeof(glm::mat4) == sizeof(Mat4View) && alignof(glm::mat4) == alignof(Mat4View),
                  "glm::mat4 changed shape: update Mat4View and the C# Matrix4x4.");
} // namespace Fenrir::Detail

template <>
struct Iris::StructMapping<Fenrir::Detail::Mat4View>
{
    static constexpr std::string_view typeName = "Fenrir.Math.Matrix4x4";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::Detail::Mat4View, column0, "Column0"),
                                                      IRIS_FIELD_AS(Fenrir::Detail::Mat4View, column1, "Column1"),
                                                      IRIS_FIELD_AS(Fenrir::Detail::Mat4View, column2, "Column2"),
                                                      IRIS_FIELD_AS(Fenrir::Detail::Mat4View, column3, "Column3"));
};

template <>
struct Iris::StructMapping<Fenrir::Colour>
{
    static constexpr std::string_view typeName = "Fenrir.Math.Colour";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Fenrir::Colour, r, "R"), IRIS_FIELD_AS(Fenrir::Colour, g, "G"),
                           IRIS_FIELD_AS(Fenrir::Colour, b, "B"), IRIS_FIELD_AS(Fenrir::Colour, a, "A"));
};

template <>
struct Iris::StructMapping<Fenrir::Math::Ray>
{
    static constexpr std::string_view typeName = "Fenrir.Math.Ray";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::Math::Ray, origin, "Origin"),
                                                      IRIS_FIELD_AS(Fenrir::Math::Ray, dir, "Direction"));
};

namespace Fenrir::Detail
{
    // Math::AABB's own mapping is FenrirPhysics's generated one (Fenrir.Physics.Aabb), and a C++ type has one mapping.
    // A struct that holds a box as Fenrir.Math's Bounds is mapped through this stand-in, with the same layout, wherever
    // the Math::AABB is.
    struct BoundsView
    {
        glm::vec3 min;
        glm::vec3 max;
    };

    static_assert(sizeof(Math::AABB) == sizeof(BoundsView) && alignof(Math::AABB) == alignof(BoundsView),
                  "Math::AABB changed shape: update BoundsView and the C# Bounds.");
} // namespace Fenrir::Detail

template <>
struct Iris::StructMapping<Fenrir::Detail::BoundsView>
{
    static constexpr std::string_view typeName = "Fenrir.Math.Bounds";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::Detail::BoundsView, min, "Min"),
                                                      IRIS_FIELD_AS(Fenrir::Detail::BoundsView, max, "Max"));
};
