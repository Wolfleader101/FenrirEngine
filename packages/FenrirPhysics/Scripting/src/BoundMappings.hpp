#pragma once

// The physics binding's own mappings of the two entity types it binds (FenrirPhysics.binding.toml, [bound-types]),
// written by hand as a binding maps its bound types; FenrirScripting/EngineMappings.hpp maps the vector, quaternion and
// Transform. The generated mappings include this header.

#include <cstdint>
#include <string_view>

#include "FenrirECS/Entity.hpp"
#include "FenrirScripting/EngineMappings.hpp"
#include "ScriptQueries.hpp"

// A Fenrir::Entity is its 32-bit value, then a pointer to its list, which C#'s EntityLink holds opaquely. Its members
// are private, so the mapping declares them by offset.
template <>
struct Iris::StructMapping<Fenrir::Entity>
{
    static constexpr std::string_view typeName = "Fenrir.Scripting.EntityLink";
    static constexpr auto fields =
        Iris::StructFields(Iris::Field<std::uint32_t>("value", 0), Iris::Field<std::uint32_t>("reserved", 4),
                           Iris::Field<void*>("owner", 8));
};

// A C# Entity's Value is an auto-property, so its field has the compiler's name.
template <>
struct Iris::StructMapping<Fenrir::ScriptQueries::EntityValue>
{
    static constexpr std::string_view typeName = "Fenrir.Scripting.Entity";
    static constexpr auto fields = Iris::StructFields(Iris::Field<std::uint32_t>("<Value>k__BackingField", 0));
};
