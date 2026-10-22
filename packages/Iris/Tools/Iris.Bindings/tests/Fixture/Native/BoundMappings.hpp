#pragma once

// The binding's own mappings of the fixture's bound types, written by hand as a binding maps its vector, quaternion and
// handle to the C# types of Contract/Bound.cs. The generated mappings include this header.

#include "Fixture.hpp"
#include "Iris/Iris.hpp"

#include <cstdint>
#include <string_view>

template <>
struct Iris::StructMapping<Fixture::Vec3>
{
    static constexpr std::string_view typeName = "Fixture.Bindings.Vector3";
    static constexpr auto fields =
        Iris::StructFields(Iris::Field<float>("X", 0), Iris::Field<float>("Y", 4), Iris::Field<float>("Z", 8));
};

template <>
struct Iris::StructMapping<Fixture::Quat>
{
    static constexpr std::string_view typeName = "Fixture.Bindings.Quaternion";
    static constexpr auto fields = Iris::StructFields(Iris::Field<float>("X", 0), Iris::Field<float>("Y", 4),
                                                      Iris::Field<float>("Z", 8), Iris::Field<float>("W", 12));
};

// Its members are private, so the mapping declares them by offset.
template <>
struct Iris::StructMapping<Fixture::Handle>
{
    static constexpr std::string_view typeName = "Fixture.Bindings.Handle";
    static constexpr auto fields =
        Iris::StructFields(Iris::Field<std::uint32_t>("Id", 0), Iris::Field<void*>("Owner", 8));
};
