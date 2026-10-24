#pragma once

// The physics binding's own mapping of the 32-bit entity value it binds (FenrirPhysics.binding.toml, [bound-types]),
// written by hand as a binding maps its bound types. The vector, quaternion, Transform and Fenrir::Entity (as C#'s
// EntityLink) are the built-in binding's, generated from FenrirECS's and FenrirMath's descriptions into
// FenrirScripting/Generated/FenrirScriptingMappings.hpp. The generated mappings include this header.

#include <cstdint>
#include <string_view>

#include "FenrirScripting/Generated/FenrirScriptingMappings.hpp"
#include "ScriptQueries.hpp"

// A C# Entity's Value is an auto-property, so its field has the compiler's name.
template <>
struct Iris::StructMapping<Fenrir::ScriptQueries::EntityValue>
{
    static constexpr std::string_view typeName = "Fenrir.Scripting.Entity";
    static constexpr auto fields = Iris::StructFields(Iris::Field<std::uint32_t>("<Value>k__BackingField", 0));
};
