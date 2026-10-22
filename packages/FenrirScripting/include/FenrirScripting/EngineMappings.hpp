#pragma once

// How the engine's math types, Transform and entity links look to C#. The math types are the Fenrir.Math contract's
// (ADR-0029): glm::vec2, vec3 and vec4 (Fenrir.Math.Vector2, Vector3 and Vector4), glm::quat (Quaternion), glm::mat4
// (Matrix4x4, through Detail::Mat4View), Fenrir::Colour (Colour), Math::Ray (Ray) and Math::AABB (Bounds). Transform
// is the scripting contract's (Fenrir.Scripting.Transform), and Detail::EntityLinkView (Fenrir.Scripting.EntityLink)
// stands in for a Fenrir::Entity. A scripting extension whose components, events or resources hold these types
// includes this header, so its mappings can name them as fields. A mapping used by a component, event or resource is
// checked against the C# struct whenever a script assembly loads, so a drift between the two fails the load and names
// the field.

#include <Iris/Iris.hpp>

#include <cstdint>
#include <string_view>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Math.hpp"

template <>
struct Iris::StructMapping<glm::vec2>
{
    static constexpr std::string_view typeName = "Fenrir.Math.Vector2";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(glm::vec2, x, "X"), IRIS_FIELD_AS(glm::vec2, y, "Y"));
};

template <>
struct Iris::StructMapping<glm::vec3>
{
    static constexpr std::string_view typeName = "Fenrir.Math.Vector3";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(glm::vec3, x, "X"), IRIS_FIELD_AS(glm::vec3, y, "Y"), IRIS_FIELD_AS(glm::vec3, z, "Z"));
};

template <>
struct Iris::StructMapping<glm::vec4>
{
    static constexpr std::string_view typeName = "Fenrir.Math.Vector4";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(glm::vec4, x, "X"), IRIS_FIELD_AS(glm::vec4, y, "Y"),
                           IRIS_FIELD_AS(glm::vec4, z, "Z"), IRIS_FIELD_AS(glm::vec4, w, "W"));
};

template <>
struct Iris::StructMapping<glm::quat>
{
    static constexpr std::string_view typeName = "Fenrir.Math.Quaternion";
    // glm keeps w first in memory, and the C# Quaternion declares its fields in that order (fields are listed in
    // declaration order, and each offset is checked).
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(glm::quat, w, "W"), IRIS_FIELD_AS(glm::quat, x, "X"),
                           IRIS_FIELD_AS(glm::quat, y, "Y"), IRIS_FIELD_AS(glm::quat, z, "Z"));
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

template <>
struct Iris::StructMapping<Fenrir::Math::AABB>
{
    static constexpr std::string_view typeName = "Fenrir.Math.Bounds";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::Math::AABB, min, "Min"),
                                                      IRIS_FIELD_AS(Fenrir::Math::AABB, max, "Max"));
};

template <>
struct Iris::StructMapping<Fenrir::Transform>
{
    static constexpr std::string_view typeName = "Fenrir.Scripting.Transform";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::Transform, pos, "Position"),
                                                      IRIS_FIELD_AS(Fenrir::Transform, rot, "Rotation"),
                                                      IRIS_FIELD_AS(Fenrir::Transform, scale, "Scale"));
};

namespace Fenrir::Detail
{
    // Fenrir::Entity is a 32-bit entity value and a pointer to its entity list. Scripts see the value; the rest is the
    // engine's. Iris cannot map an Entity, so a struct holding one (the engine's Relationship, an extension's event) is
    // mapped through a stand-in with the same layout that has this where each Entity is.
    struct EntityLinkView
    {
        std::uint32_t value;
        std::uint32_t reserved;
        std::uint64_t owner;
    };

    static_assert(sizeof(void*) == 8,
                  "Scripting maps Entity as a 32-bit value and a 64-bit pointer: it needs a 64-bit build.");
    static_assert(sizeof(Entity) == sizeof(EntityLinkView) && alignof(Entity) == alignof(EntityLinkView),
                  "Entity changed shape: update EntityLinkView and the C# EntityLink.");
} // namespace Fenrir::Detail

template <>
struct Iris::StructMapping<Fenrir::Detail::EntityLinkView>
{
    static constexpr std::string_view typeName = "Fenrir.Scripting.EntityLink";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(Fenrir::Detail::EntityLinkView, value),
                                                      IRIS_FIELD(Fenrir::Detail::EntityLinkView, reserved),
                                                      IRIS_FIELD(Fenrir::Detail::EntityLinkView, owner));
};
