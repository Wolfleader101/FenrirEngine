// Point and spot lights on the null backend through RendererProbe (the spec's seam 1; ADR-0048): every light that can
// light anything reaches the frame's light buffer at set 0 binding 4, with its count in the frame block, a hundred of
// them draw, and SpawnModel turns a glTF's KHR_lights_punctual lights into the components.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirAssets/Model.hpp"
#include "FenrirECS/ComponentType.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirRenderer/SpawnModel.hpp"
#include "RendererTestApp.hpp"

using Fenrir::Entity;
using Fenrir::RendererProbe;
using Fenrir::Graphics::BindingGroupEntry;
using Fenrir::Math::Vec3;
using RendererTest::FloatAt;
using RendererTest::RendererTestApp;
using RendererTest::UintAt;

namespace
{
    /** @brief A light buffer record's size, as `Fenrir/Frame.glsl` declares FenrirLight (std430). */
    constexpr std::size_t LightRecord = 64;
    /** @brief Where the frame block's `lights` uvec4 sits, after five vec4s. */
    constexpr std::size_t LightCountOffset = 80;

    /** @brief One record of the light buffer, as read back. */
    struct LightRecordRead
    {
        Vec3 position{0.0f};
        float range = 0.0f;
        Vec3 colour{0.0f};
        std::uint32_t kind = 0;
        Vec3 direction{0.0f};
    };

    /** @brief What the first Opaque draw of the last frame binds at set 0. */
    std::vector<BindingGroupEntry> FrameSet(const RendererTestApp& test)
    {
        const std::vector<RendererTest::RecordedDraw> draws = test.Draws();
        REQUIRE_FALSE(draws.empty());
        return RendererProbe::Entries(test.Device(), draws[0].groups.at(0));
    }

    /** @brief The frame block's light count: how many records of the light buffer the shaders read. */
    std::uint32_t LightCount(const RendererTestApp& test)
    {
        const std::vector<BindingGroupEntry> set = FrameSet(test);
        const auto bytes = RendererProbe::Bytes(test.Device(), set.at(0).buffer);
        return UintAt(bytes, set.at(0).offset + LightCountOffset);
    }

    /** @brief The first `count` records of the light buffer the last frame bound. */
    std::vector<LightRecordRead> LightRecords(const RendererTestApp& test, std::size_t count)
    {
        const std::vector<BindingGroupEntry> set = FrameSet(test);
        REQUIRE(set.size() == 5);
        REQUIRE(set[4].binding == 4);
        const auto bytes = RendererProbe::Bytes(test.Device(), set[4].buffer);
        REQUIRE(bytes.size() >= count * LightRecord);
        std::vector<LightRecordRead> records;
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::size_t at = set[4].offset + i * LightRecord;
            LightRecordRead record;
            record.position = {FloatAt(bytes, at), FloatAt(bytes, at + 4), FloatAt(bytes, at + 8)};
            record.range = FloatAt(bytes, at + 12);
            record.colour = {FloatAt(bytes, at + 16), FloatAt(bytes, at + 20), FloatAt(bytes, at + 24)};
            record.kind = UintAt(bytes, at + 28);
            record.direction = {FloatAt(bytes, at + 32), FloatAt(bytes, at + 36), FloatAt(bytes, at + 40)};
            records.push_back(record);
        }
        return records;
    }

    bool Near(Vec3 a, Vec3 b)
    {
        return glm::length(a - b) < 1e-4f;
    }

    /** @brief A scene with a camera and one lit cube in front of it. */
    void AddLitCube(RendererTestApp& test)
    {
        test.AddCamera();
        test.AddMeshRenderer(test.Mesh(), test.Material());
    }

    Entity AddPointLight(RendererTestApp& test, Vec3 position, Fenrir::PointLight light = {})
    {
        Entity entity = test.ActiveScene().GetEntityList().CreateEntity();
        entity.GetComponent<Fenrir::Transform>().pos = position;
        entity.AddComponent<Fenrir::PointLight>(light);
        return entity;
    }
} // namespace

TEST_CASE("Set 0 binds the light buffer at binding 4, and a scene without lights has a count of 0",
          "[renderer][lights]")
{
    RendererTestApp test;
    AddLitCube(test);
    test.Frame();

    const std::vector<BindingGroupEntry> set = FrameSet(test);
    REQUIRE(set.size() == 5);
    REQUIRE(set[4].binding == 4);
    REQUIRE_FALSE(set[4].buffer.IsEmpty());
    REQUIRE(LightCount(test) == 0);
    REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
}

TEST_CASE("Every point and spot light reaches the light buffer, at its world position, with its colour times its "
          "intensity",
          "[renderer][lights]")
{
    RendererTestApp test;
    AddLitCube(test);
    AddPointLight(test, {1.0f, 2.0f, 3.0f}, Fenrir::PointLight{{1.0f, 0.5f, 0.25f, 1.0f}, 2.0f, 6.0f, false});
    // A spot under a parent: its world position and its world -Z, turned to point straight down.
    Entity parent = test.ActiveScene().GetEntityList().CreateEntity();
    parent.GetComponent<Fenrir::Transform>().pos = {0.0f, 5.0f, 0.0f};
    Entity spot = test.ActiveScene().GetEntityList().CreateEntity();
    parent.AddChild(spot);
    spot.GetComponent<Fenrir::Transform>().pos = {1.0f, 0.0f, 0.0f};
    spot.GetComponent<Fenrir::Transform>().rot = Fenrir::Math::AngleAxis(-1.5707964f, Vec3(1.0f, 0.0f, 0.0f));
    spot.AddComponent<Fenrir::SpotLight>(Fenrir::SpotLight{Fenrir::Colour::White, 4.0f, 8.0f, 0.2f, 0.5f, false});
    test.Frame();

    REQUIRE(LightCount(test) == 2);
    const std::vector<LightRecordRead> records = LightRecords(test, 2);
    INFO("points first, then spots");
    REQUIRE(records[0].kind == 0);
    REQUIRE(Near(records[0].position, {1.0f, 2.0f, 3.0f}));
    REQUIRE(Near(records[0].colour, {2.0f, 1.0f, 0.5f}));
    REQUIRE(records[0].range == 6.0f);
    REQUIRE(records[1].kind == 1);
    REQUIRE(Near(records[1].position, {1.0f, 5.0f, 0.0f}));
    REQUIRE(Near(records[1].colour, {4.0f, 4.0f, 4.0f}));
    REQUIRE(records[1].range == 8.0f);
    REQUIRE(Near(records[1].direction, {0.0f, -1.0f, 0.0f}));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A light that can light nothing does not reach the light buffer", "[renderer][lights]")
{
    RendererTestApp test;
    AddLitCube(test);
    AddPointLight(test, {0.0f, 0.0f, 0.0f}, Fenrir::PointLight{Fenrir::Colour::White, 0.0f, 5.0f, false});
    AddPointLight(test, {0.0f, 0.0f, 0.0f}, Fenrir::PointLight{Fenrir::Colour::White, 1.0f, 0.0f, false});
    AddPointLight(test, {0.0f, 0.0f, 0.0f}, Fenrir::PointLight{{0.0f, 0.0f, 0.0f, 1.0f}, 1.0f, 5.0f, false});
    AddPointLight(test, {7.0f, 0.0f, 0.0f});
    test.Frame();

    REQUIRE(LightCount(test) == 1);
    REQUIRE(Near(LightRecords(test, 1)[0].position, {7.0f, 0.0f, 0.0f}));
}

TEST_CASE("A hundred lights all reach the light buffer and the scene draws, growing the buffer without stale handles",
          "[renderer][lights]")
{
    RendererTestApp test;
    AddLitCube(test);
    constexpr int Count = 100;
    for (int i = 0; i < Count; ++i)
    {
        const Vec3 at{static_cast<float>(i % 10), 1.0f, static_cast<float>(i / 10)};
        if (i % 4 == 3)
        {
            Entity spot = test.ActiveScene().GetEntityList().CreateEntity();
            spot.GetComponent<Fenrir::Transform>().pos = at;
            spot.AddComponent<Fenrir::SpotLight>();
        }
        else
        {
            AddPointLight(test, at);
        }
    }
    // Two frames, so both copies of the ring hold the grown buffer.
    test.Frame();
    test.Frame();

    REQUIRE(LightCount(test) == Count);
    std::set<std::pair<int, int>> seen;
    for (const LightRecordRead& record : LightRecords(test, Count))
    {
        seen.insert(
            {static_cast<int>(std::lround(record.position.x)), static_cast<int>(std::lround(record.position.z))});
    }
    INFO("every light is in the buffer once");
    REQUIRE(seen.size() == Count);
    REQUIRE(test.Draws().size() == 1);
    REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Moving a light moves its record the next frame", "[renderer][lights]")
{
    RendererTestApp test;
    AddLitCube(test);
    Entity light = AddPointLight(test, {1.0f, 0.0f, 0.0f});
    test.Frame();
    REQUIRE(Near(LightRecords(test, 1)[0].position, {1.0f, 0.0f, 0.0f}));

    light.GetComponent<Fenrir::Transform>().pos = {-2.0f, 3.0f, 0.5f};
    test.Frame();
    REQUIRE(Near(LightRecords(test, 1)[0].position, {-2.0f, 3.0f, 0.5f}));
}

TEST_CASE("SpawnModel gives a glTF's point and spot lights their components, with a range where the file has none",
          "[renderer][lights][spawn-model]")
{
    RendererTestApp test;
    AddLitCube(test);
    const Fenrir::LoadResult<Fenrir::Model> model =
        test.Store().Load<Fenrir::Model>(FENRIR_TEST_LIGHTS, Fenrir::AssetOwner::App());
    REQUIRE(model.Ok());
    Fenrir::SpawnModel(test.app, test.ActiveScene(), model.handle);

    Fenrir::EntityList& entities = test.ActiveScene().GetEntityList();
    std::vector<Entity> points;
    const std::array<Fenrir::ComponentType, 1> pointType{Fenrir::ComponentType::Native<Fenrir::PointLight>()};
    entities.ForEachWith(pointType, [&](std::uint32_t id) { points.push_back(entities.GetEntity(id)); });
    std::vector<Entity> spots;
    const std::array<Fenrir::ComponentType, 1> spotType{Fenrir::ComponentType::Native<Fenrir::SpotLight>()};
    entities.ForEachWith(spotType, [&](std::uint32_t id) { spots.push_back(entities.GetEntity(id)); });

    INFO("Lamp's point light and Torch's spot light; Sun's directional light is not imported");
    REQUIRE(points.size() == 1);
    REQUIRE(spots.size() == 1);
    const Fenrir::PointLight& lamp = points[0].GetComponent<Fenrir::PointLight>();
    REQUIRE(lamp.intensity == 3.0f);
    REQUIRE(lamp.range == 7.0f);
    REQUIRE(lamp.colour.g == 0.5f);
    REQUIRE_FALSE(lamp.castShadows);
    const Fenrir::SpotLight& torch = spots[0].GetComponent<Fenrir::SpotLight>();
    REQUIRE(torch.innerAngle == 0.2f);
    REQUIRE(torch.outerAngle == 0.6f);
    INFO("no range in the file: sqrt(256 x 20), where it falls to 1/256 of its light one unit away");
    REQUIRE(std::abs(torch.range - std::sqrt(256.0f * 20.0f)) < 1e-3f);

    test.Frame();
    REQUIRE(LightCount(test) == 2);
    const std::vector<LightRecordRead> records = LightRecords(test, 2);
    INFO("Lamp at its node's place, Torch pointing down: its node is turned a quarter about +X");
    REQUIRE(Near(records[0].position, {1.0f, 2.0f, 3.0f}));
    REQUIRE(Near(records[1].position, {0.0f, 4.0f, 0.0f}));
    REQUIRE(Near(records[1].direction, {0.0f, -1.0f, 0.0f}));
}
