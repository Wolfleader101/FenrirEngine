#pragma once

// The Common binding's own mapping of its bound type, written by hand as a binding maps its vector to the C# type of
// Contract/Bound.cs. The generated mappings include this header.

#include "Fixture.hpp"
#include "Iris/Iris.hpp"

#include <string_view>

template <>
struct Iris::StructMapping<Fixture::Angles>
{
    static constexpr std::string_view typeName = "Fixture.Common.Angles";
    static constexpr auto fields = Iris::StructFields(Iris::Field<float>("Pitch", 0), Iris::Field<float>("Yaw", 4));
};
