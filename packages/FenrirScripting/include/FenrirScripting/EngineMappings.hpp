#pragma once

// How the engine's math types, Transform and entity links look to C#: the Iris struct mappings of glm::vec3
// (Fenrir.Scripting.Vector3), glm::quat (Fenrir.Scripting.Quaternion), Fenrir::Transform (Fenrir.Scripting.Transform)
// and Detail::EntityLinkView (Fenrir.Scripting.EntityLink, standing in for a Fenrir::Entity). A scripting extension
// whose components or events hold these types includes this header, so its mappings can name them as fields. Each
// mapping is checked against the C# struct in the Fenrir.Scripting contract whenever a script assembly loads, so a
// drift between the two fails the load and names the field.

#include <Iris/Iris.hpp>

#include <cstdint>
#include <string_view>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Math.hpp"

template <>
struct Iris::StructMapping<glm::vec3>
{
    static constexpr std::string_view typeName = "Fenrir.Scripting.Vector3";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(glm::vec3, x, "X"), IRIS_FIELD_AS(glm::vec3, y, "Y"), IRIS_FIELD_AS(glm::vec3, z, "Z"));
};

template <>
struct Iris::StructMapping<glm::quat>
{
    static constexpr std::string_view typeName = "Fenrir.Scripting.Quaternion";
    // glm keeps w first in memory, and the C# Quaternion declares its fields in that order (fields are listed in
    // declaration order, and each offset is checked).
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(glm::quat, w, "W"), IRIS_FIELD_AS(glm::quat, x, "X"),
                           IRIS_FIELD_AS(glm::quat, y, "Y"), IRIS_FIELD_AS(glm::quat, z, "Z"));
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
