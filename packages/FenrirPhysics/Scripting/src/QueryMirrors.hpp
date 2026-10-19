#pragma once

// The C# mirrors of QueryFilter and the motion tests' hits (Managed/Fenrir.Physics/Queries.cs), and how they look to
// C#. They cross only by pointer, so nothing checks them as scripts load: the scripting tests check these mappings
// against the C# structs and read every field back.

#include <cstdint>
#include <string_view>

#include "FenrirMath/Math.hpp"
#include "FenrirScripting/EngineMappings.hpp"

namespace Fenrir::Detail
{
    // A C# Entity: its 32-bit value alone, since a Fenrir::Entity also holds a pointer to its list.
    struct EntityValueView
    {
        std::uint32_t value;
    };

    struct ScriptQueryFilter
    {
        std::uint32_t mask;
        std::uint32_t layers;
        EntityValueView ignoreBody;
        bool hitTriggers;
    };
    static_assert(sizeof(ScriptQueryFilter) == 16, "Keep ScriptQueryFilter as the C# QueryFilter lays it out.");

    struct ScriptMoveCastHit
    {
        EntityValueView body;
        EntityValueView collider;
        Math::Vec3 point;
        Math::Vec3 normal;
        float distance;
    };
    static_assert(sizeof(ScriptMoveCastHit) == 36, "Keep ScriptMoveCastHit as the C# MoveCastHit lays it out.");

    struct ScriptPenetrationHit
    {
        EntityValueView body;
        EntityValueView collider;
        Math::Vec3 point;
        Math::Vec3 normal;
        float depth;
    };
    static_assert(sizeof(ScriptPenetrationHit) == 36,
                  "Keep ScriptPenetrationHit as the C# PenetrationHit lays it out.");
} // namespace Fenrir::Detail

template <>
struct Iris::StructMapping<Fenrir::Detail::EntityValueView>
{
    static constexpr std::string_view typeName = "Fenrir.Scripting.Entity";
    // Entity's Value is an auto-property, so its field has the compiler's name.
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Fenrir::Detail::EntityValueView, value, "<Value>k__BackingField"));
};

template <>
struct Iris::StructMapping<Fenrir::Detail::ScriptQueryFilter>
{
    using Mirror = Fenrir::Detail::ScriptQueryFilter;
    static constexpr std::string_view typeName = "Fenrir.Physics.QueryFilter";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(Mirror, mask, "Mask"), IRIS_FIELD_AS(Mirror, layers, "Layers"),
        IRIS_FIELD_AS(Mirror, ignoreBody, "IgnoreBody"), IRIS_FIELD_AS(Mirror, hitTriggers, "HitTriggers"));
};

template <>
struct Iris::StructMapping<Fenrir::Detail::ScriptMoveCastHit>
{
    using Mirror = Fenrir::Detail::ScriptMoveCastHit;
    static constexpr std::string_view typeName = "Fenrir.Physics.MoveCastHit";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Mirror, body, "Body"), IRIS_FIELD_AS(Mirror, collider, "Collider"),
                           IRIS_FIELD_AS(Mirror, point, "Point"), IRIS_FIELD_AS(Mirror, normal, "Normal"),
                           IRIS_FIELD_AS(Mirror, distance, "Distance"));
};

template <>
struct Iris::StructMapping<Fenrir::Detail::ScriptPenetrationHit>
{
    using Mirror = Fenrir::Detail::ScriptPenetrationHit;
    static constexpr std::string_view typeName = "Fenrir.Physics.PenetrationHit";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Mirror, body, "Body"), IRIS_FIELD_AS(Mirror, collider, "Collider"),
                           IRIS_FIELD_AS(Mirror, point, "Point"), IRIS_FIELD_AS(Mirror, normal, "Normal"),
                           IRIS_FIELD_AS(Mirror, depth, "Depth"));
};
