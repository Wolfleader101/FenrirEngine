#pragma once

// Hand-written descriptions of the types the bridge's tests write and read, in the shape a package's generated type
// registration holds them (ADR-0079 §7): static data, one description per type. `Fixed` holds two fields of each
// fixed-layout kind and `Owned` two of each owning kind: in each pair one is left at its default by the tests and the
// other is changed, so a test sees the default left out beside the changed value written.

#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/FieldVisitor.hpp"
#include "FenrirTypes/Kind.hpp"
#include "FenrirTypes/TypeDescription.hpp"
#include "FenrirTypes/TypeRegistry.hpp"
#include "FenrirTypes/ValueRef.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace BridgeTest
{
    using Fenrir::FieldInfo;
    using Fenrir::Kind;
    using Fenrir::Role;
    using Fenrir::TypeDescription;
    using Fenrir::ValueType;

    enum class Mobility : std::uint8_t
    {
        Static,
        Movable = 7,
    };

    // A bound type: three float components.
    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    // A nested struct of its own defaults.
    struct Range
    {
        float min = 0.0f;
        float max = 1.0f;
    };

    // A tagged union of two alternatives, a box by default.
    enum class ShapeKind : std::uint8_t
    {
        Box,
        Sphere,
    };

    struct BoxShape
    {
        Vec3 halfExtents{0.5f, 0.5f, 0.5f};
    };

    struct SphereShape
    {
        float radius = 1.0f;
    };

    struct Shape
    {
        ShapeKind kind = ShapeKind::Box;
        union {
            BoxShape box;
            SphereShape sphere;
        };

        Shape() : box{}
        {
        }
    };

    // The layouts of an asset handle and of an entity, which FenrirTypes knows without naming either.
    struct HandleLayout
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;
    };

    struct EntityLayout
    {
        std::uint32_t id = 0xFFFFFFFFu;
        void* list = nullptr;
    };

    // Two fields of each fixed-layout kind, and one tagged union.
    struct Fixed
    {
        std::int32_t lives = 3;
        std::int64_t score = 0;
        std::uint8_t team = 0;
        std::uint64_t seed = 7;
        float speed = 1.5f;
        float drag = 0.0f;
        double mass = 2.0;
        double charge = 0.0;
        bool visible = true;
        bool selected = false;
        char16_t key = u'K';
        char16_t altKey = 0;
        Mobility mobility = Mobility::Static;
        Mobility fallback = Mobility::Static;
        Vec3 position;
        Vec3 scale{1.0f, 1.0f, 1.0f};
        Range range;
        Range limits;
        std::uint32_t layers[3] = {1, 2, 3};
        std::uint32_t masks[2] = {0, 0};
        char label[8] = "probe";
        char title[8] = "";
        HandleLayout mesh;
        HandleLayout material;
        EntityLayout target;
        EntityLayout owner;
        void* cache = nullptr;
        Shape shape;
    };

    // Two fields of each owning kind: walked by a visit function, as the generator writes one.
    struct Owned
    {
        std::string name;
        std::string motto = "Onward";
        std::filesystem::path icon;
        std::filesystem::path sound = "sfx/default.wav";
        std::vector<std::int32_t> tags;
        std::vector<Range> ranges;
        std::optional<float> edge;
        std::optional<float> reach = 2.0f;
    };

    inline constexpr ValueType F32{.kind = Kind::F32};
    inline constexpr ValueType U32{.kind = Kind::U32};
    inline constexpr ValueType I32{.kind = Kind::I32};

    inline const Fenrir::Enumerator MobilityEnumerators[] = {
        {.name = "Static", .value = 0},
        {.name = "Movable", .value = 7},
    };

    inline const TypeDescription MobilityDescription{
        .name = "BridgeTest::Mobility",
        .package = "FenrirSerialisationTests",
        .role = Role::Enum,
        .size = sizeof(Mobility),
        .alignment = alignof(Mobility),
        .enumerators = MobilityEnumerators,
        .underlying = Kind::U8,
    };

    inline const FieldInfo Vec3Fields[] = {
        {.name = "x", .type = F32, .offset = offsetof(Vec3, x)},
        {.name = "y", .type = F32, .offset = offsetof(Vec3, y)},
        {.name = "z", .type = F32, .offset = offsetof(Vec3, z)},
    };

    inline const TypeDescription Vec3Description{
        .name = "BridgeTest::Vec3",
        .package = "FenrirSerialisationTests",
        .role = Role::BoundType,
        .size = sizeof(Vec3),
        .alignment = alignof(Vec3),
        .construct = Fenrir::ConstructValue<Vec3>,
        .destroy = Fenrir::DestroyValue<Vec3>,
        .fields = Vec3Fields,
    };

    inline const FieldInfo RangeFields[] = {
        {.name = "min", .type = F32, .offset = offsetof(Range, min)},
        {.name = "max", .type = F32, .offset = offsetof(Range, max)},
    };

    inline const TypeDescription RangeDescription{
        .name = "BridgeTest::Range",
        .package = "FenrirSerialisationTests",
        .role = Role::Nested,
        .size = sizeof(Range),
        .alignment = alignof(Range),
        .construct = Fenrir::ConstructValue<Range>,
        .destroy = Fenrir::DestroyValue<Range>,
        .fields = RangeFields,
    };

    inline const Fenrir::Enumerator ShapeKindEnumerators[] = {
        {.name = "Box", .value = 0},
        {.name = "Sphere", .value = 1},
    };

    inline const TypeDescription ShapeKindDescription{
        .name = "BridgeTest::ShapeKind",
        .package = "FenrirSerialisationTests",
        .role = Role::Enum,
        .size = sizeof(ShapeKind),
        .alignment = alignof(ShapeKind),
        .enumerators = ShapeKindEnumerators,
        .underlying = Kind::U8,
    };

    inline const FieldInfo BoxShapeFields[] = {
        {.name = "halfExtents",
         .type = {.kind = Kind::Struct, .type = &Vec3Description},
         .offset = offsetof(BoxShape, halfExtents)},
    };

    inline const TypeDescription BoxShapeDescription{
        .name = "BridgeTest::BoxShape",
        .package = "FenrirSerialisationTests",
        .role = Role::Nested,
        .size = sizeof(BoxShape),
        .alignment = alignof(BoxShape),
        .construct = Fenrir::ConstructValue<BoxShape>,
        .destroy = Fenrir::DestroyValue<BoxShape>,
        .fields = BoxShapeFields,
    };

    inline const FieldInfo SphereShapeFields[] = {
        {.name = "radius", .type = F32, .offset = offsetof(SphereShape, radius)},
    };

    inline const TypeDescription SphereShapeDescription{
        .name = "BridgeTest::SphereShape",
        .package = "FenrirSerialisationTests",
        .role = Role::Nested,
        .size = sizeof(SphereShape),
        .alignment = alignof(SphereShape),
        .construct = Fenrir::ConstructValue<SphereShape>,
        .destroy = Fenrir::DestroyValue<SphereShape>,
        .fields = SphereShapeFields,
    };

    inline const Fenrir::Alternative ShapeAlternatives[] = {
        {.tag = 0,
         .field = {.name = "box",
                   .type = {.kind = Kind::Struct, .type = &BoxShapeDescription},
                   .offset = offsetof(Shape, box)}},
        {.tag = 1,
         .field = {.name = "sphere",
                   .type = {.kind = Kind::Struct, .type = &SphereShapeDescription},
                   .offset = offsetof(Shape, sphere)}},
    };

    inline const TypeDescription ShapeDescription{
        .name = "BridgeTest::Shape",
        .package = "FenrirSerialisationTests",
        .role = Role::Nested,
        .size = sizeof(Shape),
        .alignment = alignof(Shape),
        .construct = Fenrir::ConstructValue<Shape>,
        .destroy = Fenrir::DestroyValue<Shape>,
        .tag = {.name = "kind",
                .type = {.kind = Kind::Enum, .type = &ShapeKindDescription, .unionTag = true},
                .offset = offsetof(Shape, kind)},
        .unionOffset = offsetof(Shape, box),
        .alternatives = ShapeAlternatives,
        .defaultAlternative = 0,
    };

    inline constexpr ValueType FixedLayersType{.kind = Kind::Array, .element = &U32, .count = 3};
    inline constexpr ValueType FixedMasksType{.kind = Kind::Array, .element = &U32, .count = 2};

    inline const FieldInfo FixedFields[] = {
        {.name = "lives", .type = I32, .offset = offsetof(Fixed, lives)},
        {.name = "score", .type = {.kind = Kind::I64}, .offset = offsetof(Fixed, score)},
        {.name = "team", .type = {.kind = Kind::U8}, .offset = offsetof(Fixed, team)},
        {.name = "seed", .type = {.kind = Kind::U64}, .offset = offsetof(Fixed, seed)},
        {.name = "speed", .type = F32, .offset = offsetof(Fixed, speed)},
        {.name = "drag", .type = F32, .offset = offsetof(Fixed, drag)},
        {.name = "mass", .type = {.kind = Kind::F64}, .offset = offsetof(Fixed, mass)},
        {.name = "charge", .type = {.kind = Kind::F64}, .offset = offsetof(Fixed, charge)},
        {.name = "visible", .type = {.kind = Kind::Bool}, .offset = offsetof(Fixed, visible)},
        {.name = "selected", .type = {.kind = Kind::Bool}, .offset = offsetof(Fixed, selected)},
        {.name = "key", .type = {.kind = Kind::Char16}, .offset = offsetof(Fixed, key)},
        {.name = "altKey", .type = {.kind = Kind::Char16}, .offset = offsetof(Fixed, altKey)},
        {.name = "mobility",
         .type = {.kind = Kind::Enum, .type = &MobilityDescription},
         .offset = offsetof(Fixed, mobility)},
        {.name = "fallback",
         .type = {.kind = Kind::Enum, .type = &MobilityDescription},
         .offset = offsetof(Fixed, fallback)},
        {.name = "position",
         .type = {.kind = Kind::Struct, .type = &Vec3Description},
         .offset = offsetof(Fixed, position)},
        {.name = "scale", .type = {.kind = Kind::Struct, .type = &Vec3Description}, .offset = offsetof(Fixed, scale)},
        {.name = "range", .type = {.kind = Kind::Struct, .type = &RangeDescription}, .offset = offsetof(Fixed, range)},
        {.name = "limits",
         .type = {.kind = Kind::Struct, .type = &RangeDescription},
         .offset = offsetof(Fixed, limits)},
        {.name = "layers", .type = FixedLayersType, .offset = offsetof(Fixed, layers)},
        {.name = "masks", .type = FixedMasksType, .offset = offsetof(Fixed, masks)},
        {.name = "label", .type = {.kind = Kind::FixedString, .count = 8}, .offset = offsetof(Fixed, label)},
        {.name = "title", .type = {.kind = Kind::FixedString, .count = 8}, .offset = offsetof(Fixed, title)},
        {.name = "mesh",
         .type = {.kind = Kind::Reference, .target = "BridgeTest::Mesh"},
         .offset = offsetof(Fixed, mesh)},
        {.name = "material",
         .type = {.kind = Kind::Reference, .target = "BridgeTest::Material"},
         .offset = offsetof(Fixed, material)},
        {.name = "target", .type = {.kind = Kind::Entity}, .offset = offsetof(Fixed, target)},
        {.name = "owner", .type = {.kind = Kind::Entity}, .offset = offsetof(Fixed, owner)},
        {.name = "cache", .type = {.kind = Kind::Pointer}, .offset = offsetof(Fixed, cache)},
        {.name = "shape", .type = {.kind = Kind::Struct, .type = &ShapeDescription}, .offset = offsetof(Fixed, shape)},
    };

    inline const TypeDescription FixedDescription{
        .name = "BridgeTest::Fixed",
        .package = "FenrirSerialisationTests",
        .role = Role::Component,
        .size = sizeof(Fixed),
        .alignment = alignof(Fixed),
        .construct = Fenrir::ConstructValue<Fixed>,
        .destroy = Fenrir::DestroyValue<Fixed>,
        .fields = FixedFields,
    };

    inline constexpr ValueType RangeType{.kind = Kind::Struct, .type = &RangeDescription};

    inline const FieldInfo OwnedFields[] = {
        {.name = "name", .type = {.kind = Kind::String}},
        {.name = "motto", .type = {.kind = Kind::String}},
        {.name = "icon", .type = {.kind = Kind::Path}},
        {.name = "sound", .type = {.kind = Kind::Path}},
        {.name = "tags",
         .type = {.kind = Kind::List, .element = &I32, .list = &Fenrir::ListOperationsOf<std::int32_t>}},
        {.name = "ranges",
         .type = {.kind = Kind::List, .element = &RangeType, .list = &Fenrir::ListOperationsOf<Range>}},
        {.name = "edge",
         .type = {.kind = Kind::Optional, .element = &F32, .optional = &Fenrir::OptionalOperationsOf<float>}},
        {.name = "reach",
         .type = {.kind = Kind::Optional, .element = &F32, .optional = &Fenrir::OptionalOperationsOf<float>}},
    };

    // What the generator writes for an owning type: each field pushed once, by member, in declaration order.
    inline void VisitOwned(void* instance, Fenrir::FieldVisitor& visitor)
    {
        auto& owned = *static_cast<Owned*>(instance);
        visitor.Field(OwnedFields[0], Fenrir::ValueRef(&owned.name, OwnedFields[0].type));
        visitor.Field(OwnedFields[1], Fenrir::ValueRef(&owned.motto, OwnedFields[1].type));
        visitor.Field(OwnedFields[2], Fenrir::ValueRef(&owned.icon, OwnedFields[2].type));
        visitor.Field(OwnedFields[3], Fenrir::ValueRef(&owned.sound, OwnedFields[3].type));
        visitor.Field(OwnedFields[4], Fenrir::ValueRef(&owned.tags, OwnedFields[4].type));
        visitor.Field(OwnedFields[5], Fenrir::ValueRef(&owned.ranges, OwnedFields[5].type));
        visitor.Field(OwnedFields[6], Fenrir::ValueRef(&owned.edge, OwnedFields[6].type));
        visitor.Field(OwnedFields[7], Fenrir::ValueRef(&owned.reach, OwnedFields[7].type));
    }

    inline const TypeDescription OwnedDescription{
        .name = "BridgeTest::Owned",
        .package = "FenrirSerialisationTests",
        .role = Role::DataAsset,
        .size = sizeof(Owned),
        .alignment = alignof(Owned),
        .construct = Fenrir::ConstructValue<Owned>,
        .destroy = Fenrir::DestroyValue<Owned>,
        .fields = OwnedFields,
        .visit = VisitOwned,
    };

    // A field renamed once: files written before the rename spell `health` as `hp`.
    struct Renamed
    {
        std::int32_t health = 100;
    };

    inline constexpr std::string_view HealthFormerly[] = {"hp"};

    inline const FieldInfo RenamedFields[] = {
        {.name = "health", .type = I32, .offset = offsetof(Renamed, health), .formerly = HealthFormerly},
    };

    inline const TypeDescription RenamedDescription{
        .name = "BridgeTest::Renamed",
        .package = "FenrirSerialisationTests",
        .role = Role::Component,
        .size = sizeof(Renamed),
        .alignment = alignof(Renamed),
        .construct = Fenrir::ConstructValue<Renamed>,
        .destroy = Fenrir::DestroyValue<Renamed>,
        .fields = RenamedFields,
    };

    // Adds the types above to `registry`, as a package's generated registration would; whether every one was added.
    inline bool RegisterBridgeTypes(Fenrir::TypeRegistry& registry)
    {
        bool registered = true;
        for (const TypeDescription* description :
             {&MobilityDescription, &Vec3Description, &RangeDescription, &ShapeKindDescription, &BoxShapeDescription,
              &SphereShapeDescription, &ShapeDescription, &FixedDescription, &OwnedDescription, &RenamedDescription})
            registered = registry.Register(*description) && registered;
        return registered;
    }
} // namespace BridgeTest
