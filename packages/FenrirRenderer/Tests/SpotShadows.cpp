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
#include <catch2/generators/catch_generators.hpp>

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
    INFO("no budget, no tiles");
    REQUIRE(grid(0) == std::array<std::uint32_t, 2>{0, 0});
    INFO("squares fill their grids exactly");
    REQUIRE(grid(1) == std::array<std::uint32_t, 2>{1, 1});
    REQUIRE(grid(4) == std::array<std::uint32_t, 2>{2, 2});
    REQUIRE(grid(9) == std::array<std::uint32_t, 2>{3, 3});
    REQUIRE(grid(16) == std::array<std::uint32_t, 2>{4, 4});
    INFO("the rest take ceil(sqrt) columns and as few rows as hold them");
    REQUIRE(grid(2) == std::array<std::uint32_t, 2>{2, 1});
    REQUIRE(grid(5) == std::array<std::uint32_t, 2>{3, 2});
    REQUIRE(grid(8) == std::array<std::uint32_t, 2>{3, 3});
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
        INFO("four spot lights ask to cast");
        REQUIRE(choice.wanted == 4);
        INFO("all four keep their shadows, nearest first");
        REQUIRE(choice.lights == std::vector<std::size_t>{2, 5, 4, 0});
    }
    SECTION("past it, the nearest the budget allows")
    {
        const Fenrir::Render::SpotShadowChoice choice = ChooseSpotShadows(lights, origin, 2);
        INFO("four ask to cast");
        REQUIRE(choice.wanted == 4);
        INFO("the two nearest keep their shadows");
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
        INFO("no light keeps a shadow");
        REQUIRE(ChooseSpotShadows(lights, origin, 0).lights.empty());
    }
}

TEST_CASE("A spot shadow's atlas matrix puts its axis at its tile's centre, depth falling with distance",
          "[renderer][shadows][spot-shadows]")
{
    using namespace Fenrir::Render;
    const PunctualLight light = Spot({1.0f, 4.0f, -2.0f}, {0.3f, -1.0f, 0.2f});
    const SpotShadow shadow = FitSpotShadow(light, 1024);
    const SpotAtlasGrid grid = SpotAtlasGridOf(8);
    for (std::uint32_t tile : {0u, 4u, 7u})
    {
        CAPTURE(tile);
        const Fenrir::Math::Mat4 atlas = SpotAtlasMatrix(shadow, tile, grid);
        const float column = static_cast<float>(tile % 3);
        const float row = static_cast<float>(tile / 3);
        const Vec3 near = Projected(atlas, light.position + light.direction * 2.0f);
        const Vec3 far = Projected(atlas, light.position + light.direction * 8.0f);
        INFO("a point on the axis lands at the tile's centre, rows from the top: " << near.x << ", " << near.y);
        REQUIRE(near.x == Approx((column + 0.5f) / 3.0f).margin(1e-5));
        REQUIRE(near.y == Approx((row + 0.5f) / 3.0f).margin(1e-5));
        INFO("its reversed-Z depth falls with distance, inside (0, 1): " << near.z << ", " << far.z);
        REQUIRE(near.z > far.z);
        REQUIRE(far.z > 0.0f);
        REQUIRE(near.z < 1.0f);
    }
}

TEST_CASE("A spot shadow's tile holds its outer cone inside the filter's bounds, which lie inside the tile",
          "[renderer][shadows][spot-shadows]")
{
    using namespace Fenrir::Render;
    const PunctualLight light = Spot({1.0f, 4.0f, -2.0f}, {0.3f, -1.0f, 0.2f});
    const SpotShadow shadow = FitSpotShadow(light, 1024);
    const SpotAtlasGrid grid = SpotAtlasGridOf(8);
    const Vec3 side = glm::normalize(glm::cross(light.direction, Vec3(0.0f, 0.0f, 1.0f)));
    const Vec3 up = glm::cross(side, light.direction);
    for (std::uint32_t tile : {0u, 4u, 7u})
    {
        CAPTURE(tile);
        const GpuSpotShadow record = ToGpuSpotShadow(light, shadow, tile, grid, 1024);
        for (int step = 0; step < 8; ++step)
        {
            const float turn = static_cast<float>(step) * 0.785398f;
            const Vec3 across = std::cos(turn) * side + std::sin(turn) * up;
            const Vec3 edge = light.direction * std::cos(light.outerAngle) + across * std::sin(light.outerAngle);
            const Vec3 atlas = Projected(record.atlas, light.position + edge * 5.0f);
            INFO("the outer cone at turn " << step << " lands at " << atlas.x << ", " << atlas.y
                                           << ", inside the filter's bounds");
            REQUIRE(atlas.x >= record.bounds.x);
            REQUIRE(atlas.y >= record.bounds.y);
            REQUIRE(atlas.x <= record.bounds.z);
            REQUIRE(atlas.y <= record.bounds.w);
        }
        const float column = static_cast<float>(tile % 3);
        const float row = static_cast<float>(tile / 3);
        INFO("the bounds lie inside the tile");
        REQUIRE(record.bounds.x > column / 3.0f);
        REQUIRE(record.bounds.z < (column + 1.0f) / 3.0f);
        REQUIRE(record.bounds.y > row / 3.0f);
        REQUIRE(record.bounds.w < (row + 1.0f) / 3.0f);
    }
}

TEST_CASE("A spot shadow's record carries its light's biases and a texel's size one unit along it",
          "[renderer][shadows][spot-shadows]")
{
    using namespace Fenrir::Render;
    PunctualLight light = Spot({1.0f, 4.0f, -2.0f});
    light.shadowNormalBias = 1.5f;
    light.shadowSlopeBias = 3.0f;
    const SpotShadow shadow = FitSpotShadow(light, 1024);
    const GpuSpotShadow record = ToGpuSpotShadow(light, shadow, 0, SpotAtlasGridOf(8), 1024);
    INFO("a texel one unit along the light is the tile's width there over its texels");
    const float halfWidth = 1.0f / shadow.projection[0][0];
    REQUIRE(shadow.texelScale == Approx(2.0f * halfWidth / 1024.0f));
    INFO("the record holds the light's biases in its texels, and that texel size");
    REQUIRE(record.bias.x == 1.5f);
    REQUIRE(record.bias.y == 3.0f);
    REQUIRE(record.bias.z == shadow.texelScale);
}

TEST_CASE("Casting spot lights get one SpotShadows pass, before every view's, into a cleared Depth32Float atlas",
          "[renderer][shadows][spot-shadows]")
{
    RendererTestApp test;
    AddCubeScene(test);
    AddSpotLight(test, {0.0f, 3.0f, 0.0f});
    AddSpotLight(test, {2.0f, 3.0f, 0.0f});

    test.Frame();
    const std::vector<PassRecord> passes = SpotPasses(test);
    INFO("one pass for the frame");
    REQUIRE(passes.size() == 1);
    const std::vector<std::string> labels = RendererTest::PassesIn(RendererProbe::Submitted(test.renderer));
    INFO("ahead of the view's DepthPrepass");
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
    INFO("nothing was logged as an error, nor a warning about spot shadows");
    REQUIRE(test.messages->errors.empty());
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "spot") == 0);
}

TEST_CASE("The SpotShadows pass draws the casters into each casting light's tile, through its own view block",
          "[renderer][shadows][spot-shadows]")
{
    RendererTestApp test;
    AddCubeScene(test);
    AddSpotLight(test, {0.0f, 3.0f, 0.0f});
    AddSpotLight(test, {2.0f, 3.0f, 0.0f});
    AddSpotLight(test, {9.0f, 3.0f, 0.0f}, false);

    test.Frame();
    const std::vector<RecordedDraw> draws = test.Draws("SpotShadows");
    INFO("the cube once in each of the two casting lights' tiles");
    REQUIRE(draws.size() == 2);
    for (std::size_t t = 0; t < draws.size(); ++t)
    {
        CAPTURE(t);
        INFO("tile t along the top row");
        REQUIRE(draws[t].viewport.x == static_cast<std::int32_t>(t * 1024));
        REQUIRE(draws[t].viewport.y == 0);
        REQUIRE(draws[t].viewport.width == 1024);
        INFO("with the shadow pipeline");
        REQUIRE(RendererProbe::Label(test.Device(), draws[t].pipeline) == "Shadow");
        INFO("binding set 0's group that does not read the atlas it draws");
        REQUIRE(RendererProbe::Label(test.Device(), EntryAt(test, draws[t].groups.at(0), 8).texture) == "No shadow");
    }
    INFO("each tile through a view block of its own");
    REQUIRE(draws[0].groups.at(1) != draws[1].groups.at(1));
    INFO("RendererStats counts both");
    REQUIRE(test.renderer.Stats().shadowDraws == 2);
}

TEST_CASE("Tiles past the first row sit in the rows below, as the grid lays them out",
          "[renderer][shadows][spot-shadows]")
{
    RendererTestApp test;
    AddCubeScene(test);
    for (int i = 0; i < 5; ++i)
    {
        AddSpotLight(test, {static_cast<float>(i), 3.0f, 0.0f});
    }

    test.Frame();
    const std::vector<RecordedDraw> draws = test.Draws("SpotShadows");
    INFO("five tiles of the three by three grid");
    REQUIRE(draws.size() == 5);
    for (std::size_t t = 0; t < draws.size(); ++t)
    {
        CAPTURE(t);
        INFO("tile t at column t % 3, row t / 3, from the top left");
        REQUIRE(draws[t].viewport.x == static_cast<std::int32_t>((t % 3) * 1024));
        REQUIRE(draws[t].viewport.y == static_cast<std::int32_t>((t / 3) * 1024));
    }
}

TEST_CASE("Opaque reads the spot shadow records at set 0 binding 7 and the atlas at 8, after the SpotShadows pass",
          "[renderer][shadows][spot-shadows]")
{
    RendererTestApp test;
    AddCubeScene(test);
    AddSpotLight(test, {0.0f, 3.0f, 0.0f});

    test.Frame();
    const Fenrir::Graphics::TextureHandle atlas = SpotPasses(test).front().pass.depth->texture;
    const RecordedDraw opaque = test.Draws().front();
    INFO("the records at binding 7");
    REQUIRE_FALSE(EntryAt(test, opaque.groups.at(0), 7).buffer.IsEmpty());
    const BindingGroupEntry bound = EntryAt(test, opaque.groups.at(0), 8);
    INFO("the atlas at binding 8, with the comparison sampler the sun's atlas has");
    REQUIRE(bound.texture == atlas);
    REQUIRE(bound.sampler == EntryAt(test, opaque.groups.at(1), 2).sampler);
    const std::vector<PassRecord> opaquePasses = RendererTest::Labelled(test.Passes(), "Opaque");
    INFO("one Opaque pass, which declares sampling the atlas, so it runs after the SpotShadows pass");
    REQUIRE(opaquePasses.size() == 1);
    REQUIRE(std::ranges::any_of(opaquePasses.front().pass.uses, [&](const Fenrir::Graphics::PassResourceUse& use) {
        return use.texture == atlas && use.usage == Fenrir::Graphics::ResourceUsage::Sampled;
    }));
}

TEST_CASE("RendererProbe on null: the budget picks the nearest lights, which the light buffer holds first",
          "[renderer][shadows][spot-shadows]")
{
    Fenrir::RendererConfig config = RendererTest::NullBackend();
    config.spotShadowBudget = 2;
    RendererTestApp test(config);
    AddCubeScene(test);
    // A point light first, then four casting spot lights at rising distances from the camera at (0, 0, 5), not in
    // that order.
    Fenrir::Entity point = test.ActiveScene().GetEntityList().CreateEntity();
    point.AddComponent<Fenrir::PointLight>();
    AddSpotLight(test, {0.0f, 3.0f, -20.0f});
    AddSpotLight(test, {0.0f, 3.0f, 2.0f});
    AddSpotLight(test, {0.0f, 3.0f, -8.0f});
    AddSpotLight(test, {0.0f, 3.0f, 0.0f});

    test.Frame();
    INFO("two tiles drawn");
    REQUIRE(test.Draws("SpotShadows").size() == 2);
    INFO("five lights, two of them shadowed");
    REQUIRE(LightCounts(test) == std::array<std::uint32_t, 2>{5, 2});
    const std::vector<Vec3> positions = LightPositions(test, 5);
    INFO("the two nearest spots first, nearest first");
    REQUIRE(positions[0] == Vec3(0.0f, 3.0f, 2.0f));
    REQUIRE(positions[1] == Vec3(0.0f, 3.0f, 0.0f));
    INFO("then the point light, then the other spots");
    REQUIRE(positions[2] == Vec3(0.0f));
    REQUIRE(std::ranges::is_permutation(std::vector<Vec3>(positions.begin() + 3, positions.end()),
                                        std::vector<Vec3>{Vec3(0.0f, 3.0f, -20.0f), Vec3(0.0f, 3.0f, -8.0f)}));
}

TEST_CASE("RendererProbe on null: as the camera moves, the budget follows it", "[renderer][shadows][spot-shadows]")
{
    Fenrir::RendererConfig config = RendererTest::NullBackend();
    config.spotShadowBudget = 2;
    RendererTestApp test(config);
    Fenrir::Entity camera = AddCubeScene(test);
    AddSpotLight(test, {0.0f, 3.0f, -20.0f});
    AddSpotLight(test, {0.0f, 3.0f, 2.0f});
    AddSpotLight(test, {0.0f, 3.0f, -8.0f});
    AddSpotLight(test, {0.0f, 3.0f, 0.0f});
    test.Frame();

    camera.GetComponent<Fenrir::Transform>().pos = {0.0f, 0.0f, -15.0f};
    test.Frame();
    const std::vector<Vec3> moved = LightPositions(test, 2);
    INFO("the two spots nearest the camera's new place are shadowed, nearest first");
    REQUIRE(moved[0] == Vec3(0.0f, 3.0f, -20.0f));
    REQUIRE(moved[1] == Vec3(0.0f, 3.0f, -8.0f));
}

TEST_CASE("RendererProbe on null: spot lights past the budget are warned about once",
          "[renderer][shadows][spot-shadows]")
{
    Fenrir::RendererConfig config = RendererTest::NullBackend();
    config.spotShadowBudget = 1;
    RendererTestApp test(config);
    AddCubeScene(test);
    AddSpotLight(test, {0.0f, 3.0f, 0.0f});
    AddSpotLight(test, {2.0f, 3.0f, 0.0f});

    test.Frame();
    test.Frame();
    INFO("one warning over two frames");
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
    INFO("three by three tiles at the default budget");
    REQUIRE(RendererProbe::Desc(test.Device(), before).width == 3 * 1024);
    test.Frame();
    INFO("kept from frame to frame");
    REQUIRE(SpotPasses(test).front().pass.depth->texture == before);

    test.renderer.SetSpotShadowBudget(5);
    test.Frame();
    const Fenrir::Graphics::TextureHandle after = SpotPasses(test).front().pass.depth->texture;
    INFO("a new atlas, three by two tiles for five");
    REQUIRE(after != before);
    REQUIRE(RendererProbe::Desc(test.Device(), after).width == 3 * 1024);
    REQUIRE(RendererProbe::Desc(test.Device(), after).height == 2 * 1024);
    INFO("which Opaque binds, and no command named the old one");
    REQUIRE(EntryAt(test, test.Draws().front().groups.at(0), 8).texture == after);
    REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
}

TEST_CASE("RendererProbe on null: a budget of 0 shadows no spot light and frees the atlas",
          "[renderer][shadows][spot-shadows]")
{
    RendererTestApp test;
    AddCubeScene(test);
    AddSpotLight(test, {0.0f, 3.0f, 0.0f});
    test.Frame();
    const Fenrir::Graphics::TextureHandle atlas = SpotPasses(test).front().pass.depth->texture;

    test.renderer.SetSpotShadowBudget(0);
    test.Frame();
    INFO("no pass, and no shadowed light");
    REQUIRE(SpotPasses(test).empty());
    REQUIRE(LightCounts(test) == std::array<std::uint32_t, 2>{1, 0});
    INFO("set 0 binds the map that lights everything");
    REQUIRE(RendererProbe::Label(test.Device(), EntryAt(test, test.Draws().front().groups.at(0), 8).texture) ==
            "No shadow");
    INFO("the atlas is let go, and no command named it");
    REQUIRE_FALSE(test.Device().IsLive(atlas));
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
        INFO("no pass, and no shadowed light");
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
        INFO("no pass, and no shadowed light");
        REQUIRE(SpotPasses(test).empty());
        REQUIRE(LightCounts(test) == std::array<std::uint32_t, 2>{1, 0});
        INFO("set 0 binds the map that lights everything");
        REQUIRE(RendererProbe::Label(test.Device(), EntryAt(test, test.Draws().front().groups.at(0), 8).texture) ==
                "No shadow");
    }
}

TEST_CASE("A camera's SpotShadowAtlas debug view draws the spot shadow atlas as grey, black without one",
          "[renderer][shadows][spot-shadows]")
{
    const bool casting = GENERATE(true, false);
    CAPTURE(casting);
    RendererTestApp test;
    Fenrir::Entity camera = test.AddCamera();
    camera.GetComponent<Fenrir::Camera>().debugView = Fenrir::CameraDebugView::SpotShadowAtlas;
    test.AddMeshRenderer(test.Mesh(), test.Material());
    AddSpotLight(test, {0.0f, 3.0f, 0.0f}, casting);

    test.Frame();
    const std::vector<PassRecord> output = RendererTest::Labelled(test.Passes(), "Output");
    INFO("one Output pass");
    REQUIRE(output.size() == 1);
    const std::vector<std::string> keys = RendererProbe::Keys(test.Device(), output.front().pipeline);
    INFO("drawn through Output's OCCLUSION variant, as grey");
    REQUIRE(std::ranges::find(keys, "OCCLUSION") != keys.end());
    const Fenrir::Graphics::TextureHandle shown = EntryAt(test, output.front().groups.at(2), 0).texture;
    if (casting)
    {
        INFO("Output samples the atlas");
        REQUIRE(shown == SpotPasses(test).front().pass.depth->texture);
    }
    else
    {
        INFO("with no spot shadow, Output samples the black texture");
        REQUIRE(RendererProbe::Label(test.Device(), shown) == "Default black");
    }
}
