#pragma once

// The built-in binding's mappings of the two bound types it binds (api/FenrirScripting.binding.toml, [bound-types]):
// glm's vector and quaternion, which the contract mirrors as its own Vector3 and Quaternion. A binding maps its bound
// types by hand; everything else the engine shares with C# is generated, into
// FenrirScripting/Generated/FenrirScriptingMappings.hpp, which includes this header. Each mapping is checked against
// its C# struct whenever a script assembly loads.

#include <Iris/Iris.hpp>

#include <cstddef>
#include <string_view>

#include "FenrirMath/Math.hpp"

template <>
struct Iris::StructMapping<Fenrir::Math::Vec3>
{
    static constexpr std::string_view typeName = "Fenrir.Scripting.Vector3";
    static constexpr auto fields = Iris::StructFields(Iris::Field<float>("X", offsetof(Fenrir::Math::Vec3, x)),
                                                      Iris::Field<float>("Y", offsetof(Fenrir::Math::Vec3, y)),
                                                      Iris::Field<float>("Z", offsetof(Fenrir::Math::Vec3, z)));
};

// glm keeps w first in memory, and the C# Quaternion declares its fields in that order (fields are listed in
// declaration order, and each offset is checked).
template <>
struct Iris::StructMapping<Fenrir::Math::Quat>
{
    static constexpr std::string_view typeName = "Fenrir.Scripting.Quaternion";
    static constexpr auto fields = Iris::StructFields(Iris::Field<float>("W", offsetof(Fenrir::Math::Quat, w)),
                                                      Iris::Field<float>("X", offsetof(Fenrir::Math::Quat, x)),
                                                      Iris::Field<float>("Y", offsetof(Fenrir::Math::Quat, y)),
                                                      Iris::Field<float>("Z", offsetof(Fenrir::Math::Quat, z)));
};
