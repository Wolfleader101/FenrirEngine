// Spot shadows (ADR-0050): the budget's choice, the atlas grid and each light's perspective on the CPU, and on the null
// backend through RendererProbe (the spec's seam 1) the SpotShadows pass, the tiles it draws into, the light buffer's
// order and set 0's spot shadow bindings, and the atlas made again when the budget changes.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/SpotShadows.hpp"
#include "RendererTestApp.hpp"

using Catch::Approx;
using Fenrir::RendererProbe;
using Fenrir::Graphics::BindingGroupEntry;
using Fenrir::Graphics::LoadOp;
using Fenrir::Graphics::TextureFormat;
using Fenrir::Math::Vec3;
using Fenrir::Math::Vec4;
using Fenrir::Render::LightKind;
using Fenrir::Render::PunctualLight;
using RendererTest::PassRecord;
using RendererTest::RecordedDraw;
using RendererTest::RendererTestApp;

namespace
{
    /** @brief A spot light at `position` pointing along `direction` that asks to cast. */
    PunctualLight Spot(Vec3 position, Vec3 direction = {0.0f, -1.0f, 0.0f}, bool castShadows = true)
    {
        PunctualLight light;
        light.kind = LightKind::Spot;
        light.position = position;
        light.direction = glm::normalize(direction);
        light.colour = Vec3(1.0f);
        light.range = 20.0f;
        light.outerAngle = 0.6f;
        light.castShadows = castShadows;
        return light;
    }

    /** @brief `point` through `matrix`, divided by w. */
    Vec3 Projected(const Fenrir::Math::Mat4& matrix, Vec3 point)
    {
        const Vec4 clip = matrix * Vec4(point, 1.0f);
        return Vec3(clip) / clip.w;
    }

    /** @brief A SpotLight entity at `position`, pointing down, of intensity 1, casting unless told not to. */
    Fenrir::Entity AddSpotLight(RendererTestApp& test, Vec3 position, bool castShadows = true)
    {
        Fenrir::Entity entity = test.ActiveScene().GetEntityList().CreateEntity();
        entity.GetComponent<Fenrir::Transform>().pos = position;
        entity.GetComponent<Fenrir::Transform>().rot =
            Fenrir::Math::AngleAxis(Fenrir::Math::DegToRad(-90.0f), Vec3(1.0f, 0.0f, 0.0f));
        Fenrir::SpotLight light;
        light.castShadows = castShadows;
        entity.AddComponent<Fenrir::SpotLight>(light);
        return entity;
    }

    /** @brief What a group binds at `binding`; empty if it binds nothing there. */
    BindingGroupEntry EntryAt(const RendererTestApp& test, Fenrir::Graphics::BindingGroupHandle group,
                              std::uint32_t binding)
    {
        for (const BindingGroupEntry& entry : RendererProbe::Entries(test.Device(), group))
        {
            if (entry.binding == binding)
            {
                return entry;
            }
        }
        return {};
    }

    /** @brief The SpotShadows passes of the last frame. */
    std::vector<PassRecord> SpotPasses(const RendererTestApp& test)
    {
        return RendererTest::Labelled(test.Passes(), "SpotShadows");
    }

    /** @brief The frame block's `lights` uvec4, after five vec4s, as the first Opaque draw binds it. */
    std::array<std::uint32_t, 2> LightCounts(const RendererTestApp& test)
    {
        const std::vector<RecordedDraw> draws = test.Draws();
        REQUIRE_FALSE(draws.empty());
        const BindingGroupEntry block = EntryAt(test, draws.front().groups.at(0), 0);
        const auto bytes = RendererProbe::Bytes(test.Device(), block.buffer);
        return {RendererTest::UintAt(bytes, block.offset + 80), RendererTest::UintAt(bytes, block.offset + 84)};
    }

    /** @brief The positions of the first `count` records of the light buffer the first Opaque draw binds. */
    std::vector<Vec3> LightPositions(const RendererTestApp& test, std::size_t count)
    {
        const BindingGroupEntry lights = EntryAt(test, test.Draws().front().groups.at(0), 4);
        const auto bytes = RendererProbe::Bytes(test.Device(), lights.buffer);
        std::vector<Vec3> positions;
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::size_t at = lights.offset + i * 80;
            positions.emplace_back(RendererTest::FloatAt(bytes, at), RendererTest::FloatAt(bytes, at + 4),
                                   RendererTest::FloatAt(bytes, at + 8));
        }
        return positions;
    }

    /** @brief A camera at the origin's +Z and a cube in front of it, which casts; the camera's entity. */
    Fenrir::Entity AddCubeScene(RendererTestApp& test)
    {
        Fenrir::Entity camera = test.AddCamera({0.0f, 0.0f, 5.0f});
        test.AddMeshRenderer(test.Mesh(), test.Material());
        return camera;
    }
} // namespace

TEST_CASE("The atlas grid is as square as the budget allows", "[renderer][shadows][spot-shadows]")
{
    using Fenrir::Render::SpotAtlasGridOf;
    const auto grid = [](std::uint32_t budget) {
        const Fenrir::Render::SpotAtlasGrid made = SpotAtlasGridOf(budget);
        return std::array<std::uint32_t, 2>{made.columns, made.rows};
    };
    REQUIRE(grid(0) == std::array<std::uint32_t, 2>{0, 0});
    REQUIRE(grid(1) == std::array<std::uint32_t, 2>{1, 1});
    REQUIRE(grid(2) == std::array<std::uint32_t, 2>{2, 1});
    REQUIRE(grid(4) == std::array<std::uint32_t, 2>{2, 2});
    REQUIRE(grid(5) == std::array<std::uint32_t, 2>{3, 2});
    REQUIRE(grid(8) == std::array<std::uint32_t, 2>{3, 3});
    REQUIRE(grid(9) == std::array<std::uint32_t, 2>{3, 3});
    REQUIRE(grid(16) == std::array<std::uint32_t, 2>{4, 4});
}

TEST_CASE("The budget keeps the casting spot lights nearest to any camera, nearest first",
          "[renderer][shadows][spot-shadows]")
{
    using Fenrir::Render::ChooseSpotShadows;
    std::vector<PunctualLight> lights{Spot({10.0f, 0.0f, 0.0f}), Spot({1.0f, 0.0f, 0.0f}),
                                      Spot({3.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, false), Spot({-4.0f, 0.0f, 0.0f}),
                                      Spot({2.0f, 0.0f, 0.0f})};
    lights.insert(lights.begin() + 1, PunctualLight{});
    lights[1].castShadows = true;
    const std::array<Vec3, 1> origin{Vec3(0.0f)};

    SECTION("within the budget, every caster, nearest first, and no point light or spot not asking")
    {
        const Fenrir::Render::SpotShadowChoice choice = ChooseSpotShadows(lights, origin, 8);
        REQUIRE(choice.wanted == 4);
        REQUIRE(choice.lights == std::vector<std::size_t>{2, 5, 4, 0});
    }
    SECTION("past it, the nearest the budget allows")
    {
        const Fenrir::Render::SpotShadowChoice choice = ChooseSpotShadows(lights, origin, 2);
        REQUIRE(choice.wanted == 4);
        REQUIRE(choice.lights == std::vector<std::size_t>{2, 5});
    }
    SECTION("each light by its nearest camera")
    {
        const std::array<Vec3, 2> cameras{Vec3(0.0f), Vec3(10.0f, 0.0f, 0.0f)};
        const Fenrir::Render::SpotShadowChoice choice = ChooseSpotShadows(lights, cameras, 2);
        INFO("the light at x 10 is on the second camera");
        REQUIRE(choice.lights == std::vector<std::size_t>{0, 2});
    }
    SECTION("a budget of 0 keeps none")
    {
        REQUIRE(ChooseSpotShadows(lights, origin, 0).lights.empty());
    }
}

TEST_CASE("A spot shadow's projection holds its cone inside its tile, the axis at the tile's centre",
          "[renderer][shadows][spot-shadows]")
{
    using namespace Fenrir::Render;
    const PunctualLight light = Spot({1.0f, 4.0f, -2.0f}, {0.3f, -1.0f, 0.2f});
    const SpotShadow shadow = FitSpotShadow(light, 1024);
    const SpotAtlasGrid grid = SpotAtlasGridOf(8);

    for (std::uint32_t tile : {0u, 4u, 7u})
    {
        CAPTURE(tile);
        const GpuSpotShadow record = ToGpuSpotShadow(light, shadow, tile, grid, 1024);
        const float column = static_cast<float>(tile % 3);
        const float row = static_cast<float>(tile / 3);
        INFO("a point on the axis lands at the tile's centre, its depth falling with distance (reversed-Z)");
        const Vec3 near = Projected(record.atlas, light.position + light.direction * 2.0f);
        const Vec3 far = Projected(record.atlas, light.position + light.direction * 8.0f);
        REQUIRE(near.x == Approx((column + 0.5f) / 3.0f).margin(1e-5));
        REQUIRE(near.y == Approx((row + 0.5f) / 3.0f).margin(1e-5));
        REQUIRE(near.z > far.z);
        REQUIRE(far.z > 0.0f);
        REQUIRE(near.z < 1.0f);

        INFO("a point on the outer cone, any way round, lands inside the filter's bounds");
        const Vec3 side = glm::normalize(glm::cross(light.direction, Vec3(0.0f, 0.0f, 1.0f)));
        const Vec3 up = glm::cross(side, light.direction);
        for (int step = 0; step < 8; ++step)
        {
            const float turn = static_cast<float>(step) * 0.785398f;
            const Vec3 across = std::cos(turn) * side + std::sin(turn) * up;
            const Vec3 edge = light.direction * std::cos(light.outerAngle) + across * std::sin(light.outerAngle);
            const Vec3 atlas = Projected(record.atlas, light.position + edge * 5.0f);
            REQUIRE(atlas.x >= record.bounds.x);
            REQUIRE(atlas.y >= record.bounds.y);
            REQUIRE(atlas.x <= record.bounds.z);
            REQUIRE(atlas.y <= record.bounds.w);
        }
        INFO("the bounds lie inside the tile");
        REQUIRE(record.bounds.x > column / 3.0f);
        REQUIRE(record.bounds.z < (column + 1.0f) / 3.0f);
        REQUIRE(record.bounds.y > row / 3.0f);
        REQUIRE(record.bounds.w < (row + 1.0f) / 3.0f);
    }

    INFO("a texel one unit along the light is the tile's width there over its texels");
    const float halfWidth = 1.0f / shadow.projection[0][0];
    REQUIRE(shadow.texelScale == Approx(2.0f * halfWidth / 1024.0f));
    INFO("the light's biases, in its texels");
    const GpuSpotShadow record = ToGpuSpotShadow(light, shadow, 0, grid, 1024);
    REQUIRE(record.bias.x == 1.0f);
    REQUIRE(record.bias.y == 2.0f);
    REQUIRE(record.bias.z == shadow.texelScale);
}

TEST_CASE("Casting spot lights get a SpotShadows pass before every view's, drawing the casters into a tile each",
          "[renderer][shadows][spot-shadows]")
{
    RendererTestApp test;
    AddCubeScene(test);
    AddSpotLight(test, {0.0f, 3.0f, 0.0f});
    AddSpotLight(test, {2.0f, 3.0f, 0.0f});
    AddSpotLight(test, {9.0f, 3.0f, 0.0f}, false);

    test.Frame();
    const std::vector<PassRecord> passes = SpotPasses(test);
    INFO("one pass for the frame");
    REQUIRE(passes.size() == 1);
    const std::vector<std::string> labels = RendererTest::PassesIn(RendererProbe::Submitted(test.renderer));
    REQUIRE(std::ranges::find(labels, "SpotShadows") < std::ranges::find(labels, "DepthPrepass"));
    const Fenrir::Graphics::PassDesc& pass = passes.front().pass;
    INFO("depth only, cleared to 0, the far plane under reversed-Z");
    REQUIRE(pass.colour.empty());
    REQUIRE(pass.depth->load == LoadOp::Clear);
    REQUIRE(pass.depth->clear == 0.0f);
    const Fenrir::Graphics::TextureDesc atlas = RendererProbe::Desc(test.Device(), pass.depth->texture);
    INFO("Depth32Float, the default budget of 8 as three by three 1024-texel tiles");
    REQUIRE(atlas.format == TextureFormat::Depth32Float);
    REQUIRE(atlas.width == 3 * 1024);
    REQUIRE(atlas.height == 3 * 1024);

    const std::vector<RecordedDraw> draws = test.Draws("SpotShadows");
    INFO("the cube once in each of the two casting lights' tiles, along the top row");
    REQUIRE(draws.size() == 2);
    for (std::size_t t = 0; t < draws.size(); ++t)
    {
        REQUIRE(draws[t].viewport.x == static_cast<std::int32_t>(t * 1024));
        REQUIRE(draws[t].viewport.y == 0);
        REQUIRE(draws[t].viewport.width == 1024);
        REQUIRE(RendererProbe::Label(test.Device(), draws[t].pipeline) == "Shadow");
        INFO("binding set 0's group that does not read the atlas it draws");
        REQUIRE(RendererProbe::Label(test.Device(), EntryAt(test, draws[t].groups.at(0), 8).texture) == "No shadow");
    }
    REQUIRE(draws[0].groups.at(1) != draws[1].groups.at(1));
    INFO("RendererStats counts both");
    REQUIRE(test.renderer.Stats().shadowDraws == 2);

    INFO("Opaque binds the spot shadow records at set 0 binding 7 and the atlas at 8, with the comparison sampler");
    const RecordedDraw opaque = test.Draws().front();
    REQUIRE_FALSE(EntryAt(test, opaque.groups.at(0), 7).buffer.IsEmpty());
    const BindingGroupEntry bound = EntryAt(test, opaque.groups.at(0), 8);
    REQUIRE(bound.texture == pass.depth->texture);
    REQUIRE(bound.sampler == EntryAt(test, opaque.groups.at(1), 2).sampler);
    INFO("and declares reading it, so it runs after the pass");
    const std::vector<PassRecord> opaquePasses = RendererTest::Labelled(test.Passes(), "Opaque");
    REQUIRE(opaquePasses.size() == 1);
    REQUIRE(std::ranges::any_of(opaquePasses.front().pass.uses, [&](const Fenrir::Graphics::PassResourceUse& use) {
        return use.texture == pass.depth->texture && use.usage == Fenrir::Graphics::ResourceUsage::Sampled;
    }));
    INFO("the frame block counts three lights, two of them shadowed spots");
    REQUIRE(LightCounts(test) == std::array<std::uint32_t, 2>{3, 2});
    REQUIRE(test.messages->errors.empty());
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "spot") == 0);
}

TEST_CASE("RendererProbe on null: the budget shadows the nearest lights, first in the light buffer, warned about once",
          "[renderer][shadows][spot-shadows]")
{
    Fenrir::RendererConfig config = RendererTest::NullBackend();
    config.spotShadowBudget = 2;
    RendererTestApp test(config);
    Fenrir::Entity camera = AddCubeScene(test);
    // A point light first, then four casting spot lights at rising distances from the camera at (0, 0, 5), not in
    // that order.
    Fenrir::Entity point = test.ActiveScene().GetEntityList().CreateEntity();
    point.AddComponent<Fenrir::PointLight>();
    AddSpotLight(test, {0.0f, 3.0f, -20.0f});
    AddSpotLight(test, {0.0f, 3.0f, 2.0f});
    AddSpotLight(test, {0.0f, 3.0f, -8.0f});
    AddSpotLight(test, {0.0f, 3.0f, 0.0f});

    test.Frame();
    INFO("two tiles drawn, in a two by one atlas");
    REQUIRE(test.Draws("SpotShadows").size() == 2);
    const Fenrir::Graphics::TextureDesc atlas =
        RendererProbe::Desc(test.Device(), SpotPasses(test).front().pass.depth->texture);
    REQUIRE(atlas.width == 2 * 1024);
    REQUIRE(atlas.height == 1024);
    INFO("five lights, the two nearest spots shadowed and first, nearest first; then the point light and the rest");
    REQUIRE(LightCounts(test) == std::array<std::uint32_t, 2>{5, 2});
    const std::vector<Vec3> positions = LightPositions(test, 5);
    REQUIRE(positions[0] == Vec3(0.0f, 3.0f, 2.0f));
    REQUIRE(positions[1] == Vec3(0.0f, 3.0f, 0.0f));
    REQUIRE(positions[2] == Vec3(0.0f));
    REQUIRE(std::ranges::is_permutation(std::vector<Vec3>(positions.begin() + 3, positions.end()),
                                        std::vector<Vec3>{Vec3(0.0f, 3.0f, -20.0f), Vec3(0.0f, 3.0f, -8.0f)}));
    INFO("the two left out warned about once");
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "spot shadow budget") == 1);

    INFO("as the camera moves, the budget follows it");
    camera.GetComponent<Fenrir::Transform>().pos = {0.0f, 0.0f, -15.0f};
    test.Frame();
    const std::vector<Vec3> moved = LightPositions(test, 2);
    REQUIRE(moved[0] == Vec3(0.0f, 3.0f, -20.0f));
    REQUIRE(moved[1] == Vec3(0.0f, 3.0f, -8.0f));
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "spot shadow budget") == 1);
}

TEST_CASE("RendererProbe on null: changing the budget makes the atlas again at the new budget's size",
          "[renderer][shadows][spot-shadows]")
{
    RendererTestApp test;
    AddCubeScene(test);
    AddSpotLight(test, {0.0f, 3.0f, 0.0f});

    test.Frame();
    const Fenrir::Graphics::TextureHandle before = SpotPasses(test).front().pass.depth->texture;
    REQUIRE(RendererProbe::Desc(test.Device(), before).width == 3 * 1024);
    test.Frame();
    INFO("kept from frame to frame");
    REQUIRE(SpotPasses(test).front().pass.depth->texture == before);

    test.renderer.SetSpotShadowBudget(5);
    test.Frame();
    const Fenrir::Graphics::TextureHandle after = SpotPasses(test).front().pass.depth->texture;
    INFO("five tiles: three by two");
    REQUIRE(after != before);
    REQUIRE(RendererProbe::Desc(test.Device(), after).width == 3 * 1024);
    REQUIRE(RendererProbe::Desc(test.Device(), after).height == 2 * 1024);
    INFO("Opaque binds the new atlas");
    REQUIRE(EntryAt(test, test.Draws().front().groups.at(0), 8).texture == after);

    test.renderer.SetSpotShadowBudget(0);
    test.Frame();
    INFO("a budget of 0 shadows nothing: no pass, and set 0 binds the map that lights everything");
    REQUIRE(SpotPasses(test).empty());
    REQUIRE(RendererProbe::Label(test.Device(), EntryAt(test, test.Draws().front().groups.at(0), 8).texture) ==
            "No shadow");
    REQUIRE(LightCounts(test) == std::array<std::uint32_t, 2>{1, 0});
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "spot shadow budget") == 1);
    REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
}

TEST_CASE("Spot lights without castShadows, or nothing that casts, add no SpotShadows pass",
          "[renderer][shadows][spot-shadows]")
{
    SECTION("castShadows off")
    {
        RendererTestApp test;
        AddCubeScene(test);
        AddSpotLight(test, {0.0f, 3.0f, 0.0f}, false);
        test.Frame();
        REQUIRE(SpotPasses(test).empty());
        REQUIRE(LightCounts(test) == std::array<std::uint32_t, 2>{1, 0});
    }
    SECTION("the only item casts no shadow")
    {
        RendererTestApp test;
        test.AddCamera();
        Fenrir::Entity cube = test.AddMeshRenderer(test.Mesh(), test.Material());
        cube.GetComponent<Fenrir::MeshRenderer>().castShadows = false;
        AddSpotLight(test, {0.0f, 3.0f, 0.0f});
        test.Frame();
        REQUIRE(SpotPasses(test).empty());
        REQUIRE(LightCounts(test) == std::array<std::uint32_t, 2>{1, 0});
        REQUIRE(RendererProbe::Label(test.Device(), EntryAt(test, test.Draws().front().groups.at(0), 8).texture) ==
                "No shadow");
    }
}

TEST_CASE("A camera's SpotShadowAtlas debug view draws the spot shadow atlas as grey, black without one",
          "[renderer][shadows][spot-shadows]")
{
    RendererTestApp test;
    Fenrir::Entity camera = test.AddCamera();
    camera.GetComponent<Fenrir::Camera>().debugView = Fenrir::CameraDebugView::SpotShadowAtlas;
    test.AddMeshRenderer(test.Mesh(), test.Material());
    AddSpotLight(test, {0.0f, 3.0f, 0.0f});

    test.Frame();
    const Fenrir::Graphics::TextureHandle atlas = SpotPasses(test).front().pass.depth->texture;
    const std::vector<PassRecord> output = RendererTest::Labelled(test.Passes(), "Output");
    REQUIRE(output.size() == 1);
    INFO("Output samples the atlas through its OCCLUSION variant");
    REQUIRE(EntryAt(test, output.front().groups.at(2), 0).texture == atlas);
    const std::vector<std::string> keys = RendererProbe::Keys(test.Device(), output.front().pipeline);
    REQUIRE(std::ranges::find(keys, "OCCLUSION") != keys.end());
}
