// Point shadows (ADR-0050): the budget's choice and each light's six faces on the CPU, and on the null backend through
// RendererProbe (the spec's seam 1) the passes drawing the casters into each shadowed light's cube, as instances where
// the device can write gl_Layer from a vertex stage and a face at a time where it cannot, the light buffer's order, set
// 0's point shadow bindings, and the cube array made again when the budget changes.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/PointShadows.hpp"
#include "RendererTestApp.hpp"

using Catch::Approx;
using Fenrir::RendererProbe;
using Fenrir::Graphics::BindingGroupEntry;
using Fenrir::Graphics::LoadOp;
using Fenrir::Graphics::TextureFormat;
using Fenrir::Graphics::TextureKind;
using Fenrir::Math::Vec2;
using Fenrir::Math::Vec3;
using Fenrir::Math::Vec4;
using Fenrir::Render::CubeFaceCount;
using Fenrir::Render::LightKind;
using Fenrir::Render::PunctualLight;
using RendererTest::PassRecord;
using RendererTest::RecordedDraw;
using RendererTest::RendererTestApp;

namespace
{
    /** @brief A point light at `position` of range 10, which asks to cast unless told not to. */
    PunctualLight Point(Vec3 position, bool castShadows = true)
    {
        PunctualLight light;
        light.kind = LightKind::Point;
        light.position = position;
        light.colour = Vec3(1.0f);
        light.range = 10.0f;
        light.castShadows = castShadows;
        return light;
    }

    /**
     * @brief The world direction through `ndc` of cube face `face`, as `Fenrir/CubeFaces.glsl`'s FenrirFaceDirection
     * gives it, which is how the cube is read.
     */
    Vec3 FaceDirection(std::uint32_t face, Vec2 ndc)
    {
        const float x = ndc.x;
        const float y = ndc.y;
        const std::array<Vec3, 6> directions{Vec3(1.0f, y, -x), Vec3(-1.0f, y, x), Vec3(x, 1.0f, -y),
                                             Vec3(x, -1.0f, y), Vec3(x, y, 1.0f),  Vec3(-x, y, -1.0f)};
        return directions.at(face);
    }

    /** @brief `point` through `matrix`, divided by w. */
    Vec3 Projected(const Fenrir::Math::Mat4& matrix, Vec3 point)
    {
        const Vec4 clip = matrix * Vec4(point, 1.0f);
        return Vec3(clip) / clip.w;
    }

    /** @brief A PointLight entity at `position`, casting unless told not to. */
    Fenrir::Entity AddPointLight(RendererTestApp& test, Vec3 position, bool castShadows = true)
    {
        Fenrir::Entity entity = test.ActiveScene().GetEntityList().CreateEntity();
        entity.GetComponent<Fenrir::Transform>().pos = position;
        Fenrir::PointLight light;
        light.castShadows = castShadows;
        entity.AddComponent<Fenrir::PointLight>(light);
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

    /** @brief The frame block's `lights` uvec4, after five vec4s, as the first Opaque draw binds it. */
    std::array<std::uint32_t, 3> LightCounts(const RendererTestApp& test)
    {
        const std::vector<RecordedDraw> draws = test.Draws();
        REQUIRE_FALSE(draws.empty());
        const BindingGroupEntry block = EntryAt(test, draws.front().groups.at(0), 0);
        const auto bytes = RendererProbe::Bytes(test.Device(), block.buffer);
        return {RendererTest::UintAt(bytes, block.offset + 80), RendererTest::UintAt(bytes, block.offset + 84),
                RendererTest::UintAt(bytes, block.offset + 88)};
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

    /** @brief The user shader fixture, UserTint, which has no POINT_SHADOW variant. */
    Fenrir::AssetHandle<Fenrir::Shader> UserShader(RendererTestApp& test)
    {
        std::ifstream file(FENRIR_TEST_USER_SHADER, std::ios::binary);
        const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        REQUIRE_FALSE(bytes.empty());
        Fenrir::ShaderData data;
        for (const char byte : bytes)
        {
            data.bytes.push_back(static_cast<std::byte>(byte));
        }
        return test.Store().Add<Fenrir::Shader>(std::move(data), Fenrir::AssetOwner::App());
    }

    /** @brief A null backend app whose device cannot write gl_Layer from a vertex stage. */
    void TurnOffVertexLayer(RendererTestApp& test)
    {
        RendererProbe::SetFeatures(test.Device(), {.vertexLayer = false});
    }
} // namespace

TEST_CASE("Each cube face's turn and perspective draw what a lookup of the cube along that direction reads",
          "[renderer][shadows][point-shadows]")
{
    using namespace Fenrir::Render;
    const PunctualLight light = Point({1.0f, 4.0f, -2.0f});
    const PointShadow shadow = FitPointShadow(light, 512);
    for (std::uint32_t face = 0; face < CubeFaceCount; ++face)
    {
        CAPTURE(face);
        const Fenrir::Math::Mat4 faceToClip = shadow.projection * shadow.faceViews.at(face);
        for (const Vec2 ndc : {Vec2(0.0f), Vec2(0.5f, -0.25f), Vec2(-0.9f, 0.8f)})
        {
            CAPTURE(ndc.x, ndc.y);
            const Vec3 at = Projected(faceToClip, light.position + FaceDirection(face, ndc) * 3.0f);
            INFO("the face's own direction through a point lands on that point of the face");
            REQUIRE(at.x == Approx(ndc.x).margin(1e-5));
            REQUIRE(at.y == Approx(ndc.y).margin(1e-5));
            INFO("in front of the light, inside reversed-Z's (0, 1)");
            REQUIRE(at.z > 0.0f);
            REQUIRE(at.z < 1.0f);
        }
    }
}

TEST_CASE("A point shadow's near plane is a two-hundredth of its range, held to 0.1",
          "[renderer][shadows][point-shadows]")
{
    using namespace Fenrir::Render;
    PunctualLight light = Point({0.0f, 2.0f, 0.0f});
    INFO("a two-hundredth of the range of 10");
    REQUIRE(FitPointShadow(light, 512).near == Approx(0.05f));
    light.range = 100.0f;
    INFO("a range of 100 holds the near plane at 0.1");
    REQUIRE(FitPointShadow(light, 512).near == Approx(0.1f));
}

TEST_CASE("A point shadow's record carries its light's biases and a face's width one unit out over its texels",
          "[renderer][shadows][point-shadows]")
{
    using namespace Fenrir::Render;
    PunctualLight light = Point({0.0f, 2.0f, 0.0f});
    light.shadowNormalBias = 1.5f;
    light.shadowSlopeBias = 3.0f;
    const GpuPointShadow record = ToGpuPointShadow(light, FitPointShadow(light, 512));
    INFO("the light's normal-offset bias");
    REQUIRE(record.bias.x == 1.5f);
    INFO("its slope-scaled bias");
    REQUIRE(record.bias.y == 3.0f);
    INFO("a 90-degree face is two units wide one unit out, over 512 texels");
    REQUIRE(record.bias.z == Approx(2.0f / 512.0f));
}

TEST_CASE("The point shadow budget keeps the casting point lights nearest to any camera, and no spot light",
          "[renderer][shadows][point-shadows]")
{
    using Fenrir::Render::ChooseShadowedLights;
    std::vector<PunctualLight> lights{Point({10.0f, 0.0f, 0.0f}), Point({1.0f, 0.0f, 0.0f}),
                                      Point({3.0f, 0.0f, 0.0f}, false), Point({-4.0f, 0.0f, 0.0f})};
    PunctualLight spot = Point({0.5f, 0.0f, 0.0f});
    spot.kind = LightKind::Spot;
    lights.push_back(spot);
    const std::array<Vec3, 1> origin{Vec3(0.0f)};
    const Fenrir::Render::ShadowChoice choice = ChooseShadowedLights(lights, origin, 2, LightKind::Point);
    INFO("three point lights ask to cast");
    REQUIRE(choice.wanted == 3);
    INFO("the two nearest keep their shadows, nearest first");
    REQUIRE(choice.lights == std::vector<std::size_t>{1, 3});
}

TEST_CASE("RendererProbe on null: each shadowed point light draws its casters into six faces, as instances",
          "[renderer][shadows][point-shadows]")
{
    RendererTestApp test;
    AddCubeScene(test);
    AddPointLight(test, {0.0f, 3.0f, 0.0f});
    AddPointLight(test, {2.0f, 3.0f, 0.0f});
    AddPointLight(test, {9.0f, 3.0f, 0.0f}, false);

    test.Frame();
    const std::vector<PassRecord> passes = RendererTest::Labelled(test.Passes(), "PointShadows");
    INFO("one pass for the frame, and none a face at a time");
    REQUIRE(passes.size() == 1);
    REQUIRE(RendererTest::Labelled(test.Passes(), "PointShadowFaces").empty());
    const std::vector<std::string> labels = RendererTest::PassesIn(RendererProbe::Submitted(test.renderer));
    INFO("ahead of the view's DepthPrepass");
    REQUIRE(std::ranges::find(labels, "PointShadows") < std::ranges::find(labels, "DepthPrepass"));
    const Fenrir::Graphics::PassDesc& pass = passes.front().pass;
    INFO("depth only, every layer at once, cleared to 0");
    REQUIRE(pass.colour.empty());
    REQUIRE(pass.depth->layered);
    REQUIRE(pass.depth->load == LoadOp::Clear);
    REQUIRE(pass.depth->clear == 0.0f);
    const Fenrir::Graphics::TextureDesc cubes = RendererProbe::Desc(test.Device(), pass.depth->texture);
    INFO("a Depth32Float cube array of the default budget's 4 cubes, 512-texel faces");
    REQUIRE(cubes.kind == TextureKind::CubeArray);
    REQUIRE(cubes.format == TextureFormat::Depth32Float);
    REQUIRE(cubes.layers == 4);
    REQUIRE(cubes.width == 512);
    REQUIRE(cubes.height == 512);

    const std::vector<RecordedDraw> draws = test.Draws("PointShadows");
    INFO("the cube once for each of the two casting lights");
    REQUIRE(draws.size() == 2);
    for (const RecordedDraw& draw : draws)
    {
        INFO("six instances, one a face, over the whole face");
        REQUIRE(draw.draw.instanceCount == CubeFaceCount);
        REQUIRE(draw.viewport == Fenrir::Graphics::PixelRect{0, 0, 512, 512});
        INFO("through the item's POINT_SHADOW variant");
        REQUIRE(RendererProbe::Label(test.Device(), draw.pipeline) == "PointShadow");
        const std::vector<std::string> keys = RendererProbe::Keys(test.Device(), draw.pipeline);
        REQUIRE(std::ranges::find(keys, "POINT_SHADOW") != keys.end());
        INFO("binding set 0's group that does not read the cubes it draws");
        REQUIRE(RendererProbe::Label(test.Device(), EntryAt(test, draw.groups.at(0), 10).texture) == "No point shadow");
    }
    INFO("each light through a view block of its own");
    REQUIRE(draws[0].groups.at(1) != draws[1].groups.at(1));
    INFO("RendererStats counts both");
    REQUIRE(test.renderer.Stats().shadowDraws == 2);
    INFO("nothing was logged as an error, nor a warning about point shadows");
    REQUIRE(test.messages->errors.empty());
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "point") == 0);
}

TEST_CASE("RendererProbe on null: a device without vertex-stage layers draws each face in a pass of its own",
          "[renderer][shadows][point-shadows]")
{
    RendererTestApp test;
    TurnOffVertexLayer(test);
    AddCubeScene(test);
    AddPointLight(test, {0.0f, 3.0f, 0.0f});
    AddPointLight(test, {2.0f, 3.0f, 0.0f});

    test.Frame();
    INFO("no instanced pass");
    REQUIRE(RendererTest::Labelled(test.Passes(), "PointShadows").empty());
    const std::vector<PassRecord> faces = RendererTest::Labelled(test.Passes(), "PointShadowFaces");
    INFO("six faces of each of the two cubes");
    REQUIRE(faces.size() == 2 * CubeFaceCount);
    for (std::uint32_t layer = 0; layer < faces.size(); ++layer)
    {
        CAPTURE(layer);
        const Fenrir::Graphics::PassDesc& pass = faces[layer].pass;
        INFO("each one layer of the cube array, in order, cleared");
        REQUIRE_FALSE(pass.depth->layered);
        REQUIRE(pass.depth->layer == layer);
        REQUIRE(pass.depth->load == LoadOp::Clear);
    }
    const std::vector<RecordedDraw> draws = test.Draws("PointShadowFaces");
    INFO("the cube once a face, one instance each, through a view block a face");
    REQUIRE(draws.size() == 2 * CubeFaceCount);
    for (const RecordedDraw& draw : draws)
    {
        REQUIRE(draw.draw.instanceCount == 1);
        const std::vector<std::string> keys = RendererProbe::Keys(test.Device(), draw.pipeline);
        REQUIRE(std::ranges::find(keys, "POINT_SHADOW") == keys.end());
    }
    REQUIRE(draws[0].groups.at(1) != draws[1].groups.at(1));
    INFO("RendererStats counts every face");
    REQUIRE(test.renderer.Stats().shadowDraws == 2 * CubeFaceCount);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("RendererProbe on null: a caster whose shader has no POINT_SHADOW variant is drawn a face at a time",
          "[renderer][shadows][point-shadows]")
{
    RendererTestApp test;
    AddCubeScene(test);
    test.AddMeshRenderer(test.Mesh(), test.Store().CreateMaterial(UserShader(test), Fenrir::AssetOwner::App()));
    AddPointLight(test, {0.0f, 3.0f, 0.0f});

    test.Frame();
    INFO("the built-in material's cube as six instances, clearing every layer");
    const std::vector<PassRecord> layered = RendererTest::Labelled(test.Passes(), "PointShadows");
    REQUIRE(layered.size() == 1);
    REQUIRE(test.Draws("PointShadows").size() == 1);
    const std::vector<PassRecord> faces = RendererTest::Labelled(test.Passes(), "PointShadowFaces");
    INFO("the user shader's cube a face at a time after it, loading what the instanced pass drew");
    REQUIRE(faces.size() == CubeFaceCount);
    REQUIRE(faces.front().pass.depth->load == LoadOp::Load);
    REQUIRE(test.Draws("PointShadowFaces").size() == CubeFaceCount);
    const std::vector<std::string> labels = RendererTest::PassesIn(RendererProbe::Submitted(test.renderer));
    INFO("the instanced pass first, since it clears every layer");
    REQUIRE(std::ranges::find(labels, "PointShadows") < std::ranges::find(labels, "PointShadowFaces"));
}

TEST_CASE("RendererProbe on null: the point budget picks the nearest lights, which follow the shadowed spots",
          "[renderer][shadows][point-shadows]")
{
    Fenrir::RendererConfig config = RendererTest::NullBackend();
    config.pointShadowBudget = 2;
    RendererTestApp test(config);
    AddCubeScene(test);
    AddPointLight(test, {0.0f, 3.0f, -20.0f});
    AddPointLight(test, {0.0f, 3.0f, 2.0f});
    AddPointLight(test, {0.0f, 3.0f, -8.0f});
    AddPointLight(test, {0.0f, 3.0f, 0.0f});
    // A casting spot light, which comes first.
    Fenrir::Entity spot = test.ActiveScene().GetEntityList().CreateEntity();
    spot.GetComponent<Fenrir::Transform>().pos = {5.0f, 5.0f, 5.0f};
    Fenrir::SpotLight spotLight;
    spotLight.castShadows = true;
    spot.AddComponent<Fenrir::SpotLight>(spotLight);

    test.Frame();
    INFO("two cubes drawn");
    REQUIRE(test.Draws("PointShadows").size() == 2);
    INFO("five lights: one shadowed spot, then two shadowed points");
    REQUIRE(LightCounts(test) == std::array<std::uint32_t, 3>{5, 1, 2});
    const std::vector<Vec3> positions = LightPositions(test, 5);
    INFO("the shadowed spot first");
    REQUIRE(positions[0] == Vec3(5.0f, 5.0f, 5.0f));
    INFO("the two nearest points next, nearest first");
    REQUIRE(positions[1] == Vec3(0.0f, 3.0f, 2.0f));
    REQUIRE(positions[2] == Vec3(0.0f, 3.0f, 0.0f));
    INFO("then the other points");
    REQUIRE(std::ranges::is_permutation(std::vector<Vec3>(positions.begin() + 3, positions.end()),
                                        std::vector<Vec3>{Vec3(0.0f, 3.0f, -20.0f), Vec3(0.0f, 3.0f, -8.0f)}));
}

TEST_CASE("RendererProbe on null: point lights past the budget are warned about once",
          "[renderer][shadows][point-shadows]")
{
    Fenrir::RendererConfig config = RendererTest::NullBackend();
    config.pointShadowBudget = 1;
    RendererTestApp test(config);
    AddCubeScene(test);
    AddPointLight(test, {0.0f, 3.0f, 0.0f});
    AddPointLight(test, {2.0f, 3.0f, 0.0f});

    test.Frame();
    test.Frame();
    INFO("one warning over two frames");
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "point shadow budget") == 1);
}

TEST_CASE("Opaque reads the point shadow records at set 0 binding 9 and the cubes at 10, after drawing them",
          "[renderer][shadows][point-shadows]")
{
    RendererTestApp test;
    AddCubeScene(test);
    AddPointLight(test, {0.0f, 3.0f, 0.0f});

    test.Frame();
    const Fenrir::Graphics::TextureHandle cubes =
        RendererTest::Labelled(test.Passes(), "PointShadows").front().pass.depth->texture;
    const RecordedDraw opaque = test.Draws().front();
    INFO("the records at binding 9");
    REQUIRE_FALSE(EntryAt(test, opaque.groups.at(0), 9).buffer.IsEmpty());
    const BindingGroupEntry bound = EntryAt(test, opaque.groups.at(0), 10);
    INFO("the cubes at binding 10, with the atlases' comparison sampler");
    REQUIRE(bound.texture == cubes);
    REQUIRE(bound.sampler == EntryAt(test, opaque.groups.at(0), 8).sampler);
    const std::vector<PassRecord> opaquePasses = RendererTest::Labelled(test.Passes(), "Opaque");
    INFO("Opaque declares sampling the cubes, so it runs after the pass drawing them");
    REQUIRE(std::ranges::any_of(opaquePasses.front().pass.uses, [&](const Fenrir::Graphics::PassResourceUse& use) {
        return use.texture == cubes && use.usage == Fenrir::Graphics::ResourceUsage::Sampled;
    }));
}

namespace
{
    /** @brief The point shadow cubes the last frame's PointShadows pass drew into. */
    Fenrir::Graphics::TextureHandle CubesDrawn(const RendererTestApp& test)
    {
        return RendererTest::Labelled(test.Passes(), "PointShadows").front().pass.depth->texture;
    }
} // namespace

TEST_CASE("RendererProbe on null: changing the point budget makes the cubes again at the new budget's size",
          "[renderer][shadows][point-shadows]")
{
    RendererTestApp test;
    AddCubeScene(test);
    AddPointLight(test, {0.0f, 3.0f, 0.0f});

    test.Frame();
    const Fenrir::Graphics::TextureHandle before = CubesDrawn(test);
    test.Frame();
    INFO("kept from frame to frame");
    REQUIRE(CubesDrawn(test) == before);

    test.renderer.SetPointShadowBudget(2);
    test.Frame();
    const Fenrir::Graphics::TextureHandle after = CubesDrawn(test);
    INFO("new cubes, two of them");
    REQUIRE(after != before);
    REQUIRE(RendererProbe::Desc(test.Device(), after).layers == 2);
    INFO("which Opaque binds, and no command named the old ones");
    REQUIRE(EntryAt(test, test.Draws().front().groups.at(0), 10).texture == after);
    REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
}

TEST_CASE("RendererProbe on null: a point budget of 0 shadows no point light and frees the cubes",
          "[renderer][shadows][point-shadows]")
{
    RendererTestApp test;
    AddCubeScene(test);
    AddPointLight(test, {0.0f, 3.0f, 0.0f});
    test.Frame();
    const Fenrir::Graphics::TextureHandle cubes = CubesDrawn(test);

    test.renderer.SetPointShadowBudget(0);
    test.Frame();
    INFO("no pass and no shadowed light");
    REQUIRE(RendererTest::Labelled(test.Passes(), "PointShadows").empty());
    REQUIRE(LightCounts(test) == std::array<std::uint32_t, 3>{1, 0, 0});
    INFO("set 0 binds the cube that lights everything");
    REQUIRE(RendererProbe::Label(test.Device(), EntryAt(test, test.Draws().front().groups.at(0), 10).texture) ==
            "No point shadow");
    INFO("the cubes are let go, and no command named them");
    REQUIRE_FALSE(test.Device().IsLive(cubes));
    REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
}

TEST_CASE("Point lights without castShadows, or nothing that casts, add no point shadow pass",
          "[renderer][shadows][point-shadows]")
{
    SECTION("castShadows off")
    {
        RendererTestApp test;
        AddCubeScene(test);
        AddPointLight(test, {0.0f, 3.0f, 0.0f}, false);
        test.Frame();
        INFO("no pass, and no shadowed light");
        REQUIRE(RendererTest::Labelled(test.Passes(), "PointShadows").empty());
        REQUIRE(LightCounts(test) == std::array<std::uint32_t, 3>{1, 0, 0});
    }
    SECTION("the only item casts no shadow")
    {
        RendererTestApp test;
        test.AddCamera();
        Fenrir::Entity cube = test.AddMeshRenderer(test.Mesh(), test.Material());
        cube.GetComponent<Fenrir::MeshRenderer>().castShadows = false;
        AddPointLight(test, {0.0f, 3.0f, 0.0f});
        test.Frame();
        INFO("no pass of either kind, and no shadowed light");
        REQUIRE(RendererTest::Labelled(test.Passes(), "PointShadows").empty());
        REQUIRE(RendererTest::Labelled(test.Passes(), "PointShadowFaces").empty());
        REQUIRE(LightCounts(test) == std::array<std::uint32_t, 3>{1, 0, 0});
    }
}
