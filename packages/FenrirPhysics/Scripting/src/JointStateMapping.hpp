#pragma once

// How JointFrame and JointState look to C#. JointState crosses only by pointer, from GetJointState, so nothing checks
// it as scripts load: the scripting tests check this mapping against the C# mirror (Managed/Fenrir.Physics/Joints.cs)
// and read every field back. JointFrame's mapping is here because JointState holds one; Joint's uses it too.

#include <string_view>

#include "FenrirPhysics/Joints.hpp"
#include "FenrirScripting/EngineMappings.hpp"

template <>
struct Iris::StructMapping<Fenrir::JointFrame>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.JointFrame";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::JointFrame, position, "Position"),
                                                      IRIS_FIELD_AS(Fenrir::JointFrame, rotation, "Rotation"));
};

template <>
struct Iris::StructMapping<Fenrir::JointState>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.JointState";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::JointState, force, "Force"),
                                                      IRIS_FIELD_AS(Fenrir::JointState, torque, "Torque"),
                                                      IRIS_FIELD_AS(Fenrir::JointState, relative, "Relative"),
                                                      IRIS_FIELD_AS(Fenrir::JointState, position, "Position"));
};
