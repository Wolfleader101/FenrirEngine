// The goldens through the bridge (the spec, "Testing", the third test of each): a hand-registered MyGame::Sword with
// its MyGame::ParticleEffect sub-asset, and test components named as the scene's, go through ToTree under an envelope
// the test builds by hand, and WriteText of that envelope is the golden file byte for byte.
#include "FenrirSerialisation/Bridge.hpp"
#include "FenrirSerialisation/Text.hpp"
#include "FenrirSerialisation/Value.hpp"

#include "GoldenTypes.hpp"
#include "SerialisationTest.hpp"

#include "FenrirTypes/TypeRegistry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

using namespace Fenrir::Serialisation;
using namespace GoldenTest;
using SerialisationTest::ReadFixture;

namespace
{
    // An envelope's `type`, `version` and `data`: the value's canonical name, its type's version and its tree.
    void AddTyped(Object& envelope, const void* value, const Fenrir::TypeDescription& type,
                  const Fenrir::TypeRegistry& registry)
    {
        envelope.Add("type", type.name);
        envelope.Add("version", type.version);
        envelope.Add("data", ToTree(value, type, registry, Resolvers{}));
    }

    template <std::size_t N>
    void Copy(char (&to)[N], const char* text)
    {
        const std::size_t length = std::min(std::strlen(text), N - 1);
        std::memcpy(to, text, length);
        to[length] = '\0';
    }

    Value SwordEnvelope(const Fenrir::TypeRegistry& registry)
    {
        Sword sword;
        sword.displayName = "Iron Sword";
        sword.damage = 12.5f;
        sword.durability = std::numeric_limits<float>::infinity();
        sword.mesh = "models/sword.glb#Mesh0";
        sword.tags = {"melee", "one-handed", "iron"};
        sword.upgrades = {{1, 100, 1.5f}, {2, 250, 3.25f}, {3, 600, 6.0f}};
        sword.onHit = "#sparks";

        ParticleEffect sparks;
        sparks.texture = "fx/spark.png";
        sparks.count = 24;
        sparks.lifetime = 0.4f;

        Object sparksEnvelope;
        AddTyped(sparksEnvelope, &sparks, ParticleEffectDescription, registry);
        Object sub;
        sub.Add("sparks", std::move(sparksEnvelope));

        Object envelope;
        envelope.Add("format", 1);
        AddTyped(envelope, &sword, SwordDescription, registry);
        envelope.Add("sub", std::move(sub));

        Value root(std::move(envelope));
        root.leadingComments.push_back(
            "// Iron sword: tuning numbers; `description` is left out, so it takes the type's default");
        return root;
    }

    // One entity of the scene: its id, its parent's, and its components each under its type's canonical name.
    class EntityWriter
    {
      public:
        EntityWriter(const Fenrir::TypeRegistry& registry, const char* id, const char* parent) : m_registry(registry)
        {
            m_entity.Add("id", id);
            if (parent != nullptr)
                m_entity.Add("parent", parent);
        }

        EntityWriter& Component(const void* value, const Fenrir::TypeDescription& type)
        {
            m_components.Add(std::string(type.name), ToTree(value, type, m_registry, Resolvers{}));
            return *this;
        }

        Object Done()
        {
            m_entity.Add("components", std::move(m_components));
            return std::move(m_entity);
        }

      private:
        const Fenrir::TypeRegistry& m_registry;
        Object m_entity;
        Object m_components;
    };

    Name Named(const char* text)
    {
        Name name;
        Copy(name.value, text);
        return name;
    }

    MeshRenderer Renderer(const char* mesh, const char* material)
    {
        MeshRenderer renderer;
        Copy(renderer.mesh, mesh);
        Copy(renderer.material, material);
        return renderer;
    }

    Fenrir::Math::Quat Rotation(float x, float y, float z, float w)
    {
        Fenrir::Math::Quat rotation;
        rotation.x = x;
        rotation.y = y;
        rotation.z = z;
        rotation.w = w;
        return rotation;
    }

    Value SceneEnvelope(const Fenrir::TypeRegistry& registry)
    {
        Sky sky;
        Copy(sky.cubemap, "textures/sky/day.ktx2");
        sky.exposure = 1.2f;
        Object resources;
        resources.Add(std::string(SkyDescription.name), ToTree(&sky, SkyDescription, registry, Resolvers{}));

        const Name level = Named("Level");

        const Name playerName = Named("Player");
        Transform playerTransform;
        playerTransform.position = Fenrir::Math::Vec3(0.0f, 1.8f, 0.0f);
        playerTransform.rotation = Rotation(0.0f, 0.7071068f, 0.0f, 0.7071068f);
        const MeshRenderer playerRenderer = Renderer("models/player.glb#Mesh0", "materials/player.mat");

        const Name swordName = Named("Sword");
        const MeshRenderer swordRenderer = Renderer("models/sword.glb#Mesh0", "materials/iron.mat");
        BoneAttachment attachment;
        Copy(attachment.skeleton, "d4e5f");
        Copy(attachment.bone, "hand_r");

        const Name sunName = Named("Sun");
        Transform sunTransform;
        sunTransform.rotation = Rotation(-0.3826834f, 0.0f, 0.0f, 0.9238795f);
        DirectionalLight light;
        light.color = Rgb{1.0f, 0.95f, 0.8f};
        light.intensity = 3.5f;
        light.shadows = ShadowMode::Soft;

        Array entities;
        entities.Add(EntityWriter(registry, "a7k2q", nullptr).Component(&level, NameDescription).Done());
        entities.Add(EntityWriter(registry, "d4e5f", "a7k2q")
                         .Component(&playerName, NameDescription)
                         .Component(&playerTransform, TransformDescription)
                         .Component(&playerRenderer, MeshRendererDescription)
                         .Done());
        entities.Add(EntityWriter(registry, "m9x1p", "d4e5f")
                         .Component(&swordName, NameDescription)
                         .Component(&swordRenderer, MeshRendererDescription)
                         .Component(&attachment, BoneAttachmentDescription)
                         .Done());
        entities.Add(EntityWriter(registry, "s3w8c", "a7k2q")
                         .Component(&sunName, NameDescription)
                         .Component(&sunTransform, TransformDescription)
                         .Component(&light, DirectionalLightDescription)
                         .Done());

        Object envelope;
        envelope.Add("format", 1);
        envelope.Add("type", "Fenrir::Scene");
        envelope.Add("version", 1);
        envelope.Add("resources", std::move(resources));
        envelope.Add("entities", std::move(entities));

        Value root(std::move(envelope));
        root.leadingComments.push_back("// Level 1: the player's start room; entities are written parent-first");
        return root;
    }
} // namespace

TEST_CASE("A hand-registered sword through ToTree writes sword.asset.json5 byte for byte",
          "[serialisation][golden][totree]")
{
    Fenrir::TypeRegistry registry;
    {
        const bool registered = RegisterGoldenTypes(registry);
        INFO("FenrirMath's types and the goldens' register without a refusal");
        REQUIRE(registered);
    }

    const std::string golden = ReadFixture("sword.asset.json5");

    INFO("WriteText of the sword and its sparks written by ToTree, under a hand-built envelope, is the golden file");
    REQUIRE(WriteText(SwordEnvelope(registry)) == golden);
}

TEST_CASE("Hand-registered scene components through ToTree write level1.scene.json5 byte for byte",
          "[serialisation][golden][totree]")
{
    Fenrir::TypeRegistry registry;
    {
        const bool registered = RegisterGoldenTypes(registry);
        INFO("FenrirMath's types and the goldens' register without a refusal");
        REQUIRE(registered);
    }

    const std::string golden = ReadFixture("level1.scene.json5");

    INFO("WriteText of the sky and every component written by ToTree, under a hand-built envelope, is the golden file");
    REQUIRE(WriteText(SceneEnvelope(registry)) == golden);
}
