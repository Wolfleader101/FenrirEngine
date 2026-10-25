#pragma once

// How EntityValue crosses to C# (ADR-0044): as a field of a shared struct it is a C# Entity, mapped by hand as a
// binding's bound types are, and by value it is a wrapper, crossing as its one field under Iris's wrapper trait. Every
// binding that names Fenrir::EntityValue in its [bound-types] lists this header in its [cpp] includes.

#include <Iris/Iris.hpp>

#include <cstdint>
#include <string_view>

#include "FenrirScripting/EntityValue.hpp"

// A C# Entity's Value is an auto-property, so its field has the compiler's name.
template <>
struct Iris::StructMapping<Fenrir::EntityValue>
{
    static constexpr std::string_view typeName = "Fenrir.Scripting.Entity";
    static constexpr auto fields = Iris::StructFields(Iris::Field<std::uint32_t>("<Value>k__BackingField", 0));
};

template <>
struct Iris::WrapperOf<Fenrir::EntityValue>
{
    using Value = std::uint32_t;

    static Fenrir::EntityValue From(Value value) noexcept
    {
        return Fenrir::EntityValue(value);
    }

    static Value To(Fenrir::EntityValue entity) noexcept
    {
        return entity.value;
    }
};
