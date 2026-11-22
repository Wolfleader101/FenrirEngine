// The goldens (the spec, "Testing"): the format research's sword.asset.json5 and level1.scene.json5, committed in
// fixtures/ as the research wrote them. A tree built in code, comments included, writes each one byte for byte, and
// each one read then written gives back its own bytes.
#include "FenrirSerialisation/Text.hpp"

#include "SerialisationTest.hpp"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>

using namespace Fenrir::Serialisation;
using SerialisationTest::ReadFixture;

namespace
{
    Object Row(int level, int cost, float damageBonus)
    {
        Object row;
        row.Add("level", level);
        row.Add("cost", cost);
        row.Add("damageBonus", damageBonus);
        return row;
    }

    Value Sword()
    {
        Array tags;
        tags.Add("melee");
        tags.Add("one-handed");
        tags.Add("iron");

        Array upgrades;
        upgrades.Add(Row(1, 100, 1.5f));
        upgrades.Add(Row(2, 250, 3.25f));
        upgrades.Add(Row(3, 600, 6.0f));

        Object data;
        data.Add("displayName", "Iron Sword");
        data.Add("damage", 12.5f);
        data.Add("durability", std::numeric_limits<float>::infinity());
        data.Add("mesh", "models/sword.glb#Mesh0");
        data.Add("tags", std::move(tags));
        data.Add("upgrades", std::move(upgrades));
        data.Add("onHit", "#sparks");

        Object sparksData;
        sparksData.Add("texture", "fx/spark.png");
        sparksData.Add("count", 24);
        sparksData.Add("lifetime", 0.4f);

        Object sparks;
        sparks.Add("type", "MyGame::ParticleEffect");
        sparks.Add("version", 1);
        sparks.Add("data", std::move(sparksData));

        Object sub;
        sub.Add("sparks", std::move(sparks));

        Object envelope;
        envelope.Add("format", 1);
        envelope.Add("type", "MyGame::Sword");
        envelope.Add("version", 2);
        envelope.Add("data", std::move(data));
        envelope.Add("sub", std::move(sub));

        Value root(std::move(envelope));
        root.leadingComments.push_back(
            "// Iron sword: tuning numbers; `description` is left out, so it takes the type's default");
        return root;
    }

    Array Floats(std::initializer_list<float> components)
    {
        Array array;
        for (const float component : components)
            array.Add(component);
        return array;
    }

    Object Named(const char* name)
    {
        Object value;
        value.Add("value", name);
        return value;
    }

    Object MeshRenderer(const char* mesh, const char* material)
    {
        Object renderer;
        renderer.Add("mesh", mesh);
        renderer.Add("material", material);
        return renderer;
    }

    Object Entity(const char* id, const char* parent, Object components)
    {
        Object entity;
        entity.Add("id", id);
        if (parent != nullptr)
            entity.Add("parent", parent);
        entity.Add("components", std::move(components));
        return entity;
    }

    Value Scene()
    {
        Object sky;
        sky.Add("cubemap", "textures/sky/day.ktx2");
        sky.Add("exposure", 1.2f);
        Object resources;
        resources.Add("Fenrir::Sky", std::move(sky));

        Object level;
        level.Add("Fenrir::Name", Named("Level"));

        Object playerTransform;
        playerTransform.Add("position", Floats({0.0f, 1.8f, 0.0f}));
        playerTransform.Add("rotation", Floats({0.0f, 0.7071068f, 0.0f, 0.7071068f}));
        Object player;
        player.Add("Fenrir::Name", Named("Player"));
        player.Add("Fenrir::Transform", std::move(playerTransform));
        player.Add("Fenrir::MeshRenderer", MeshRenderer("models/player.glb#Mesh0", "materials/player.mat"));

        Object attachment;
        attachment.Add("skeleton", "d4e5f");
        attachment.Add("bone", "hand_r");
        Object sword;
        sword.Add("Fenrir::Name", Named("Sword"));
        sword.Add("Fenrir::MeshRenderer", MeshRenderer("models/sword.glb#Mesh0", "materials/iron.mat"));
        sword.Add("MyGame::BoneAttachment", std::move(attachment));

        Object sunTransform;
        sunTransform.Add("rotation", Floats({-0.3826834f, 0.0f, 0.0f, 0.9238795f}));
        Object light;
        light.Add("color", Floats({1.0f, 0.95f, 0.8f}));
        light.Add("intensity", 3.5f);
        light.Add("shadows", "Soft");
        Object sun;
        sun.Add("Fenrir::Name", Named("Sun"));
        sun.Add("Fenrir::Transform", std::move(sunTransform));
        sun.Add("Fenrir::DirectionalLight", std::move(light));

        Array entities;
        entities.Add(Entity("a7k2q", nullptr, std::move(level)));
        entities.Add(Entity("d4e5f", "a7k2q", std::move(player)));
        entities.Add(Entity("m9x1p", "d4e5f", std::move(sword)));
        entities.Add(Entity("s3w8c", "a7k2q", std::move(sun)));

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

TEST_CASE("The golden fixtures are the research's samples, unchanged", "[serialisation][golden]")
{
    INFO("sword.asset.json5 is the research's 688 bytes and level1.scene.json5 its 1,451");
    REQUIRE(ReadFixture("sword.asset.json5").size() == 688);
    REQUIRE(ReadFixture("level1.scene.json5").size() == 1451);
}

TEST_CASE("A sword tree built in code writes sword.asset.json5 byte for byte", "[serialisation][golden]")
{
    const std::string golden = ReadFixture("sword.asset.json5");

    INFO("WriteText of the hand-built sword envelope is the golden file");
    REQUIRE(WriteText(Sword()) == golden);
}

TEST_CASE("A scene tree built in code writes level1.scene.json5 byte for byte", "[serialisation][golden]")
{
    const std::string golden = ReadFixture("level1.scene.json5");

    INFO("WriteText of the hand-built scene envelope is the golden file");
    REQUIRE(WriteText(Scene()) == golden);
}

TEST_CASE("sword.asset.json5 read then written gives back its own bytes, comments included", "[serialisation][golden]")
{
    const std::string golden = ReadFixture("sword.asset.json5");
    SerialisationTest::RecordingLogger logger;

    const ReadTreeResult result = ReadText(golden, "sword.asset.json5", logger);

    INFO("the golden reads without a diagnostic");
    REQUIRE(result.tree.has_value());
    REQUIRE(result.diagnostics.empty());

    INFO("WriteText of the tree read is the golden file, its leading comment kept");
    REQUIRE(WriteText(*result.tree) == golden);
}

TEST_CASE("level1.scene.json5 read then written gives back its own bytes, comments included", "[serialisation][golden]")
{
    const std::string golden = ReadFixture("level1.scene.json5");
    SerialisationTest::RecordingLogger logger;

    const ReadTreeResult result = ReadText(golden, "level1.scene.json5", logger);

    INFO("the golden reads without a diagnostic");
    REQUIRE(result.tree.has_value());
    REQUIRE(result.diagnostics.empty());

    INFO("WriteText of the tree read is the golden file, its leading comment kept");
    REQUIRE(WriteText(*result.tree) == golden);
}
