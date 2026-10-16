#pragma once

// How the engine's math types and Transform look to C#: the Iris struct mappings of glm::vec3
// (Fenrir.Scripting.Vector3), glm::quat (Fenrir.Scripting.Quaternion) and Fenrir::Transform
// (Fenrir.Scripting.Transform). A scripting extension whose components hold these types includes this header, so its
// mappings can name them as fields. Each mapping is checked against the C# struct in the Fenrir.Scripting contract
// whenever a script assembly loads, so a drift between the two fails the load and names the field.

#include <Iris/Iris.hpp>

#include <string_view>

#include "FenrirECS/DefaultComponents.hpp"
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
