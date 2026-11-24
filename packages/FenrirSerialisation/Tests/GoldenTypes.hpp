#pragma once

// Hand-written descriptions of the types the goldens hold (the spec, "Testing"): the sword data asset and its particle
// effect sub-asset, and test components named as the scene's. They are the test's own types, not the engine's: the
// fields a file names an asset by are fixed strings or strings here, so the goldens need no resolvers (Resolvers.cpp
// tests those). The transform holds FenrirMath's real Vec3 and Quat, through their registered descriptions.

#include "FenrirMath/Generated/FenrirMathTypes.hpp"
#include "FenrirMath/Math.hpp"

#include "FenrirTypes/Describe.hpp"
#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/FieldVisitor.hpp"
#include "FenrirTypes/Kind.hpp"
#include "FenrirTypes/TypeDescription.hpp"
#include "FenrirTypes/TypeRegistry.hpp"
#include "FenrirTypes/ValueRef.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace GoldenTest
{
    using Fenrir::FieldInfo;
    using Fenrir::Kind;
    using Fenrir::Role;
    using Fenrir::TypeDescription;
    using Fenrir::ValueType;

    // MyGame::Sword's upgrade table row.
    struct Upgrade
    {
        std::int32_t level = 0;
        std::int32_t cost = 0;
        float damageBonus = 0.0f;
    };

    // MyGame::Sword: `description` is declared but the golden leaves it at its default.
    struct Sword
    {
        std::string displayName;
        std::string description;
        float damage = 10.0f;
        float durability = 100.0f;
        std::string mesh;
        std::vector<std::string> tags;
        std::vector<Upgrade> upgrades;
        std::string onHit;
    };

    // MyGame::ParticleEffect, the sword's sub-asset.
    struct ParticleEffect
    {
        std::filesystem::path texture;
        std::int32_t count = 0;
        float lifetime = 1.0f;
    };

    // The scene's components and its sky resource, each fixed-layout.
    struct Name
    {
        char value[32] = "";
    };

    struct Transform
    {
        Fenrir::Math::Vec3 position{0.0f, 0.0f, 0.0f};
        Fenrir::Math::Quat rotation = Fenrir::Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
        Fenrir::Math::Vec3 scale{1.0f, 1.0f, 1.0f};
    };

    struct MeshRenderer
    {
        char mesh[64] = "";
        char material[64] = "";
    };

    struct BoneAttachment
    {
        char skeleton[16] = "";
        char bone[32] = "";
    };

    // The sample's light colour: three components, red, green and blue.
    struct Rgb
    {
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
    };

    enum class ShadowMode : std::uint8_t
    {
        None,
        Hard,
        Soft,
    };

    struct DirectionalLight
    {
        Rgb color;
        float intensity = 1.0f;
        ShadowMode shadows = ShadowMode::None;
    };

    struct Sky
    {
        char cubemap[64] = "";
        float exposure = 1.0f;
    };

    inline constexpr ValueType F32{.kind = Kind::F32};
    inline constexpr ValueType I32{.kind = Kind::I32};
    inline constexpr ValueType StringType{.kind = Kind::String};

    inline const FieldInfo UpgradeFields[] = {
        {.name = "level", .type = I32, .offset = offsetof(Upgrade, level)},
        {.name = "cost", .type = I32, .offset = offsetof(Upgrade, cost)},
        {.name = "damageBonus", .type = F32, .offset = offsetof(Upgrade, damageBonus)},
    };

    inline const TypeDescription UpgradeDescription{
        .name = "MyGame::Upgrade",
        .package = "FenrirSerialisationTests",
        .role = Role::Nested,
        .size = sizeof(Upgrade),
        .alignment = alignof(Upgrade),
        .construct = Fenrir::ConstructValue<Upgrade>,
        .destroy = Fenrir::DestroyValue<Upgrade>,
        .fields = UpgradeFields,
    };

    inline constexpr ValueType UpgradeType{.kind = Kind::Struct, .type = &UpgradeDescription};

    inline const FieldInfo SwordFields[] = {
        {.name = "displayName", .type = StringType},
        {.name = "description", .type = StringType},
        {.name = "damage", .type = F32},
        {.name = "durability", .type = F32},
        {.name = "mesh", .type = StringType},
        {.name = "tags",
         .type = {.kind = Kind::List, .element = &StringType, .list = &Fenrir::ListOperationsOf<std::string>}},
        {.name = "upgrades",
         .type = {.kind = Kind::List, .element = &UpgradeType, .list = &Fenrir::ListOperationsOf<Upgrade>}},
        {.name = "onHit", .type = StringType},
    };

    inline void VisitSword(void* instance, Fenrir::FieldVisitor& visitor)
    {
        auto& sword = *static_cast<Sword*>(instance);
        visitor.Field(SwordFields[0], Fenrir::ValueRef(&sword.displayName, SwordFields[0].type));
        visitor.Field(SwordFields[1], Fenrir::ValueRef(&sword.description, SwordFields[1].type));
        visitor.Field(SwordFields[2], Fenrir::ValueRef(&sword.damage, SwordFields[2].type));
        visitor.Field(SwordFields[3], Fenrir::ValueRef(&sword.durability, SwordFields[3].type));
        visitor.Field(SwordFields[4], Fenrir::ValueRef(&sword.mesh, SwordFields[4].type));
        visitor.Field(SwordFields[5], Fenrir::ValueRef(&sword.tags, SwordFields[5].type));
        visitor.Field(SwordFields[6], Fenrir::ValueRef(&sword.upgrades, SwordFields[6].type));
        visitor.Field(SwordFields[7], Fenrir::ValueRef(&sword.onHit, SwordFields[7].type));
    }

    inline const TypeDescription SwordDescription{
        .name = "MyGame::Sword",
        .package = "FenrirSerialisationTests",
        .role = Role::DataAsset,
        .version = 2,
        .size = sizeof(Sword),
        .alignment = alignof(Sword),
        .construct = Fenrir::ConstructValue<Sword>,
        .destroy = Fenrir::DestroyValue<Sword>,
        .fields = SwordFields,
        .visit = VisitSword,
    };

    inline const FieldInfo ParticleEffectFields[] = {
        {.name = "texture", .type = {.kind = Kind::Path}},
        {.name = "count", .type = I32},
        {.name = "lifetime", .type = F32},
    };

    inline void VisitParticleEffect(void* instance, Fenrir::FieldVisitor& visitor)
    {
        auto& effect = *static_cast<ParticleEffect*>(instance);
        visitor.Field(ParticleEffectFields[0], Fenrir::ValueRef(&effect.texture, ParticleEffectFields[0].type));
        visitor.Field(ParticleEffectFields[1], Fenrir::ValueRef(&effect.count, ParticleEffectFields[1].type));
        visitor.Field(ParticleEffectFields[2], Fenrir::ValueRef(&effect.lifetime, ParticleEffectFields[2].type));
    }

    inline const TypeDescription ParticleEffectDescription{
        .name = "MyGame::ParticleEffect",
        .package = "FenrirSerialisationTests",
        .role = Role::DataAsset,
        .size = sizeof(ParticleEffect),
        .alignment = alignof(ParticleEffect),
        .construct = Fenrir::ConstructValue<ParticleEffect>,
        .destroy = Fenrir::DestroyValue<ParticleEffect>,
        .fields = ParticleEffectFields,
        .visit = VisitParticleEffect,
    };

    inline const FieldInfo NameFields[] = {
        {.name = "value", .type = {.kind = Kind::FixedString, .count = 32}, .offset = offsetof(Name, value)},
    };

    inline const TypeDescription NameDescription{
        .name = "Fenrir::Name",
        .package = "FenrirSerialisationTests",
        .role = Role::Component,
        .size = sizeof(Name),
        .alignment = alignof(Name),
        .construct = Fenrir::ConstructValue<Name>,
        .destroy = Fenrir::DestroyValue<Name>,
        .fields = NameFields,
    };

    inline const FieldInfo TransformFields[] = {
        {.name = "position",
         .type = {.kind = Kind::Struct, .type = &Fenrir::Describe<Fenrir::Math::Vec3>::Description()},
         .offset = offsetof(Transform, position)},
        {.name = "rotation",
         .type = {.kind = Kind::Struct, .type = &Fenrir::Describe<Fenrir::Math::Quat>::Description()},
         .offset = offsetof(Transform, rotation)},
        {.name = "scale",
         .type = {.kind = Kind::Struct, .type = &Fenrir::Describe<Fenrir::Math::Vec3>::Description()},
         .offset = offsetof(Transform, scale)},
    };

    inline const TypeDescription TransformDescription{
        .name = "Fenrir::Transform",
        .package = "FenrirSerialisationTests",
        .role = Role::Component,
        .size = sizeof(Transform),
        .alignment = alignof(Transform),
        .construct = Fenrir::ConstructValue<Transform>,
        .destroy = Fenrir::DestroyValue<Transform>,
        .fields = TransformFields,
    };

    inline const FieldInfo MeshRendererFields[] = {
        {.name = "mesh", .type = {.kind = Kind::FixedString, .count = 64}, .offset = offsetof(MeshRenderer, mesh)},
        {.name = "material",
         .type = {.kind = Kind::FixedString, .count = 64},
         .offset = offsetof(MeshRenderer, material)},
    };

    inline const TypeDescription MeshRendererDescription{
        .name = "Fenrir::MeshRenderer",
        .package = "FenrirSerialisationTests",
        .role = Role::Component,
        .size = sizeof(MeshRenderer),
        .alignment = alignof(MeshRenderer),
        .construct = Fenrir::ConstructValue<MeshRenderer>,
        .destroy = Fenrir::DestroyValue<MeshRenderer>,
        .fields = MeshRendererFields,
    };

    inline const FieldInfo BoneAttachmentFields[] = {
        {.name = "skeleton",
         .type = {.kind = Kind::FixedString, .count = 16},
         .offset = offsetof(BoneAttachment, skeleton)},
        {.name = "bone", .type = {.kind = Kind::FixedString, .count = 32}, .offset = offsetof(BoneAttachment, bone)},
    };

    inline const TypeDescription BoneAttachmentDescription{
        .name = "MyGame::BoneAttachment",
        .package = "FenrirSerialisationTests",
        .role = Role::Component,
        .size = sizeof(BoneAttachment),
        .alignment = alignof(BoneAttachment),
        .construct = Fenrir::ConstructValue<BoneAttachment>,
        .destroy = Fenrir::DestroyValue<BoneAttachment>,
        .fields = BoneAttachmentFields,
    };

    inline const FieldInfo RgbFields[] = {
        {.name = "r", .type = F32, .offset = offsetof(Rgb, r)},
        {.name = "g", .type = F32, .offset = offsetof(Rgb, g)},
        {.name = "b", .type = F32, .offset = offsetof(Rgb, b)},
    };

    inline const TypeDescription RgbDescription{
        .name = "GoldenTest::Rgb",
        .package = "FenrirSerialisationTests",
        .role = Role::BoundType,
        .size = sizeof(Rgb),
        .alignment = alignof(Rgb),
        .construct = Fenrir::ConstructValue<Rgb>,
        .destroy = Fenrir::DestroyValue<Rgb>,
        .fields = RgbFields,
    };

    inline const Fenrir::Enumerator ShadowModeEnumerators[] = {
        {.name = "None", .value = 0},
        {.name = "Hard", .value = 1},
        {.name = "Soft", .value = 2},
    };

    inline const TypeDescription ShadowModeDescription{
        .name = "GoldenTest::ShadowMode",
        .package = "FenrirSerialisationTests",
        .role = Role::Enum,
        .size = sizeof(ShadowMode),
        .alignment = alignof(ShadowMode),
        .enumerators = ShadowModeEnumerators,
        .underlying = Kind::U8,
    };

    inline const FieldInfo DirectionalLightFields[] = {
        {.name = "color",
         .type = {.kind = Kind::Struct, .type = &RgbDescription},
         .offset = offsetof(DirectionalLight, color)},
        {.name = "intensity", .type = F32, .offset = offsetof(DirectionalLight, intensity)},
        {.name = "shadows",
         .type = {.kind = Kind::Enum, .type = &ShadowModeDescription},
         .offset = offsetof(DirectionalLight, shadows)},
    };

    inline const TypeDescription DirectionalLightDescription{
        .name = "Fenrir::DirectionalLight",
        .package = "FenrirSerialisationTests",
        .role = Role::Component,
        .size = sizeof(DirectionalLight),
        .alignment = alignof(DirectionalLight),
        .construct = Fenrir::ConstructValue<DirectionalLight>,
        .destroy = Fenrir::DestroyValue<DirectionalLight>,
        .fields = DirectionalLightFields,
    };

    inline const FieldInfo SkyFields[] = {
        {.name = "cubemap", .type = {.kind = Kind::FixedString, .count = 64}, .offset = offsetof(Sky, cubemap)},
        {.name = "exposure", .type = F32, .offset = offsetof(Sky, exposure)},
    };

    inline const TypeDescription SkyDescription{
        .name = "Fenrir::Sky",
        .package = "FenrirSerialisationTests",
        .role = Role::SceneResource,
        .size = sizeof(Sky),
        .alignment = alignof(Sky),
        .construct = Fenrir::ConstructValue<Sky>,
        .destroy = Fenrir::DestroyValue<Sky>,
        .fields = SkyFields,
    };

    // A registry holding FenrirMath's types and the goldens', as the packages' generated registrations would fill it.
    // Whether each registered is the caller's to check.
    inline bool RegisterGoldenTypes(Fenrir::TypeRegistry& registry)
    {
        bool registered = Fenrir::Generated::RegisterFenrirMathTypes(registry);
        for (const TypeDescription* description :
             {&UpgradeDescription, &SwordDescription, &ParticleEffectDescription, &NameDescription,
              &TransformDescription, &MeshRendererDescription, &BoneAttachmentDescription, &RgbDescription,
              &ShadowModeDescription, &DirectionalLightDescription, &SkyDescription})
            registered = registry.Register(*description) && registered;
        return registered;
    }
} // namespace GoldenTest
