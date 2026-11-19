#pragma once

// Hand-written descriptions of a few test types, in the shape a package's generated type registration will hold them:
// static data, one description per type, and a Describe<T> specialisation yielding the same object.

#include "FenrirTypes/Describe.hpp"
#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/Kind.hpp"
#include "FenrirTypes/TypeDescription.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace TypesTest
{
    using Fenrir::FieldInfo;
    using Fenrir::Kind;
    using Fenrir::Role;
    using Fenrir::TypeDescription;
    using Fenrir::ValueType;

    // A Transform-shaped component: three nested structs of floats.
    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct Quat
    {
        float w = 1.0f;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct Transform
    {
        Vec3 position;
        Quat rotation;
        Vec3 scale{1.0f, 1.0f, 1.0f};
    };

    enum class Mobility : std::uint8_t
    {
        Static,
        Movable = 7,
    };

    // The layouts of FenrirAssets' AssetHandle<T> and FenrirECS's Entity, which FenrirTypes knows without naming.
    struct HandleLayout
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;
    };

    struct EntityLayout
    {
        std::uint32_t id = 0;
        void* list = nullptr;
    };

    // A fixed-layout component holding one of each other fixed-layout kind.
    struct Probe
    {
        Mobility mobility = Mobility::Movable;
        bool visible = true;
        char label[8] = "probe";
        std::uint32_t layers[3] = {1, 2, 3};
        HandleLayout mesh;
        EntityLayout target;
        char16_t key = u'K';
        double weight = 2.5;
    };

    // A two-step chain of data assets: Sword extends WeaponData, which extends nothing.
    // A fixed-layout struct held in a data asset's list, so walked by its offsets.
    struct Gem
    {
        char colour[16] = "";
        std::int32_t carats = 1;
    };

    struct WeaponData
    {
        std::string name;
        float damage = 10.0f;
    };

    struct Sword : WeaponData
    {
        std::vector<Gem> gems;
        std::optional<float> edge;
        std::filesystem::path icon;
    };

    inline constexpr ValueType F32{.kind = Kind::F32};
    inline constexpr ValueType U32{.kind = Kind::U32};
    inline constexpr ValueType I32{.kind = Kind::I32};

    inline const FieldInfo Vec3Fields[] = {
        {.name = "x", .type = F32, .offset = offsetof(Vec3, x)},
        {.name = "y", .type = F32, .offset = offsetof(Vec3, y)},
        {.name = "z", .type = F32, .offset = offsetof(Vec3, z)},
    };

    inline const TypeDescription Vec3Description{
        .name = "TypesTest::Vec3",
        .package = "FenrirTypesTests",
        .role = Role::BoundType,
        .size = sizeof(Vec3),
        .alignment = alignof(Vec3),
        .construct = Fenrir::ConstructValue<Vec3>,
        .destroy = Fenrir::DestroyValue<Vec3>,
        .fields = Vec3Fields,
    };

    inline const FieldInfo QuatFields[] = {
        {.name = "w", .type = F32, .offset = offsetof(Quat, w)},
        {.name = "x", .type = F32, .offset = offsetof(Quat, x)},
        {.name = "y", .type = F32, .offset = offsetof(Quat, y)},
        {.name = "z", .type = F32, .offset = offsetof(Quat, z)},
    };

    inline const TypeDescription QuatDescription{
        .name = "TypesTest::Quat",
        .package = "FenrirTypesTests",
        .role = Role::BoundType,
        .size = sizeof(Quat),
        .alignment = alignof(Quat),
        .construct = Fenrir::ConstructValue<Quat>,
        .destroy = Fenrir::DestroyValue<Quat>,
        .fields = QuatFields,
    };

    inline constexpr std::string_view TransformFormerly[] = {"TypesTest::Placement", "TypesTest::Pose"};
    inline constexpr std::string_view ScaleFormerly[] = {"size"};

    inline const FieldInfo TransformFields[] = {
        {.name = "position",
         .type = {.kind = Kind::Struct, .type = &Vec3Description},
         .offset = offsetof(Transform, position),
         .doc = "Where the entity is, relative to its parent.",
         .metadata = {.group = "Placement", .units = "m"}},
        {.name = "rotation",
         .type = {.kind = Kind::Struct, .type = &QuatDescription},
         .offset = offsetof(Transform, rotation)},
        {.name = "scale",
         .type = {.kind = Kind::Struct, .type = &Vec3Description},
         .offset = offsetof(Transform, scale),
         .formerly = ScaleFormerly,
         .metadata = {.range = Fenrir::FieldRange{0.0, 100.0}, .step = 0.5}},
    };

    inline const TypeDescription TransformDescription{
        .name = "TypesTest::Transform",
        .package = "FenrirTypesTests",
        .role = Role::Component,
        .version = 2,
        .formerly = TransformFormerly,
        .size = sizeof(Transform),
        .alignment = alignof(Transform),
        .construct = Fenrir::ConstructValue<Transform>,
        .destroy = Fenrir::DestroyValue<Transform>,
        .doc = "An entity's place relative to its parent.",
        .fields = TransformFields,
    };

    inline const Fenrir::Enumerator MobilityEnumerators[] = {
        {.name = "Static", .value = 0},
        {.name = "Movable", .value = 7, .doc = "Moves every frame."},
    };

    inline const TypeDescription MobilityDescription{
        .name = "TypesTest::Mobility",
        .package = "FenrirTypesTests",
        .role = Role::Enum,
        .size = sizeof(Mobility),
        .alignment = alignof(Mobility),
        .enumerators = MobilityEnumerators,
        .underlying = Kind::U8,
    };

    inline constexpr ValueType ProbeLayersType{.kind = Kind::Array, .element = &U32, .count = 3};

    inline const FieldInfo ProbeFields[] = {
        {.name = "mobility",
         .type = {.kind = Kind::Enum, .type = &MobilityDescription},
         .offset = offsetof(Probe, mobility)},
        {.name = "visible", .type = {.kind = Kind::Bool}, .offset = offsetof(Probe, visible)},
        {.name = "label",
         .type = {.kind = Kind::FixedString, .count = 8},
         .offset = offsetof(Probe, label),
         .metadata = {.display = "Label", .multiline = true}},
        {.name = "layers", .type = ProbeLayersType, .offset = offsetof(Probe, layers)},
        {.name = "mesh",
         .type = {.kind = Kind::Reference, .target = "TypesTest::Mesh"},
         .offset = offsetof(Probe, mesh)},
        {.name = "target", .type = {.kind = Kind::Entity}, .offset = offsetof(Probe, target)},
        {.name = "key", .type = {.kind = Kind::Char16}, .offset = offsetof(Probe, key), .metadata = {.readonly = true}},
        {.name = "weight",
         .type = {.kind = Kind::F64},
         .offset = offsetof(Probe, weight),
         .metadata = {.hidden = true}},
    };

    inline const TypeDescription ProbeDescription{
        .name = "TypesTest::Probe",
        .package = "FenrirTypesTests",
        .role = Role::Component,
        .size = sizeof(Probe),
        .alignment = alignof(Probe),
        .construct = Fenrir::ConstructValue<Probe>,
        .destroy = Fenrir::DestroyValue<Probe>,
        .fields = ProbeFields,
    };

    // The data assets' fields have no offsets: an owning type is walked by a generated function, not a table, so the
    // tests here reach their values through value references made from the fields' types.
    inline constexpr ValueType StringType{.kind = Kind::String};

    inline const FieldInfo GemFields[] = {
        {.name = "colour", .type = {.kind = Kind::FixedString, .count = 16}, .offset = offsetof(Gem, colour)},
        {.name = "carats", .type = I32, .offset = offsetof(Gem, carats)},
    };

    inline const TypeDescription GemDescription{
        .name = "TypesTest::Gem",
        .package = "FenrirTypesTests",
        .role = Role::Nested,
        .size = sizeof(Gem),
        .alignment = alignof(Gem),
        .construct = Fenrir::ConstructValue<Gem>,
        .destroy = Fenrir::DestroyValue<Gem>,
        .fields = GemFields,
    };

    inline const FieldInfo WeaponDataFields[] = {
        {.name = "name", .type = StringType},
        {.name = "damage", .type = F32},
    };

    inline const TypeDescription WeaponDataDescription{
        .name = "TypesTest::WeaponData",
        .package = "FenrirTypesTests",
        .role = Role::DataAsset,
        .size = sizeof(WeaponData),
        .alignment = alignof(WeaponData),
        .construct = Fenrir::ConstructValue<WeaponData>,
        .destroy = Fenrir::DestroyValue<WeaponData>,
        .fields = WeaponDataFields,
    };

    inline constexpr ValueType GemType{.kind = Kind::Struct, .type = &GemDescription};

    inline const FieldInfo SwordFields[] = {
        {.name = "gems", .type = {.kind = Kind::List, .element = &GemType, .list = &Fenrir::ListOperationsOf<Gem>}},
        {.name = "edge",
         .type = {.kind = Kind::Optional, .element = &F32, .optional = &Fenrir::OptionalOperationsOf<float>}},
        {.name = "icon", .type = {.kind = Kind::Path}},
    };

    inline const TypeDescription SwordDescription{
        .name = "TypesTest::Sword",
        .package = "FenrirTypesTests",
        .role = Role::DataAsset,
        .size = sizeof(Sword),
        .alignment = alignof(Sword),
        .construct = Fenrir::ConstructValue<Sword>,
        .destroy = Fenrir::DestroyValue<Sword>,
        .base = "TypesTest::WeaponData",
        .fields = SwordFields,
    };
} // namespace TypesTest

template <>
struct Fenrir::Describe<TypesTest::Transform>
{
    static const TypeDescription& Description()
    {
        return TypesTest::TransformDescription;
    }
};

template <>
struct Fenrir::Describe<TypesTest::Sword>
{
    static const TypeDescription& Description()
    {
        return TypesTest::SwordDescription;
    }
};
