// The depth prepass on the null backend through App::RunFrame (the spec's seam 1, ADR-0049): which items the
// DepthPrepass pass draws and in which order, with which pipelines and set 2, its targets, and Opaque testing its depth
// for equality.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "RendererTestApp.hpp"
#include "SkinningTestApp.hpp"

using Fenrir::RendererProbe;
using Fenrir::Graphics::CompareOp;
using Fenrir::Graphics::LoadOp;
using Fenrir::Graphics::TextureFormat;
using RendererTest::PassRecord;
using RendererTest::RecordedDraw;
using RendererTest::RendererTestApp;

namespace
{
    /** @brief A quad at depth `z`, facing the camera at z = 5, drawn with `material`. */
    void AddQuadAt(RendererTestApp& test, float z, Fenrir::AssetHandle<Fenrir::Material> material)
    {
        test.AddMeshRenderer(test.Mesh(RendererTest::TexturedQuad(1.0f, z)), material);
    }

    /** @brief A material on `shader` with `mode`. */
    Fenrir::AssetHandle<Fenrir::Material> Blended(RendererTestApp& test, Fenrir::BlendMode mode,
                                                  std::string_view shader = "builtin:lit")
    {
        const auto material = test.Material(shader);
        test.Store().SetBlendMode(material, mode);
        return material;
    }

    /** @brief The user shader fixture, UserTint, read from the container file fenrir_add_shaders wrote. */
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

    /** @brief Each draw's record of the draw buffer: which item it drew. */
    std::vector<std::uint32_t> Items(const std::vector<RecordedDraw>& draws)
    {
        std::vector<std::uint32_t> items;
        for (const RecordedDraw& draw : draws)
        {
            items.push_back(draw.draw.firstInstance);
        }
        return items;
    }
} // namespace

TEST_CASE("The depth prepass draws exactly the opaque and masked items, in Opaque's order, and no transparent one",
          "[renderer][prepass]")
{
    RendererTestApp test;
    test.AddCamera();
    AddQuadAt(test, 0.0f, Blended(test, Fenrir::BlendMode::AlphaBlend));
    AddQuadAt(test, 0.0f, Blended(test, Fenrir::BlendMode::Masked));
    AddQuadAt(test, -1.0f, Blended(test, Fenrir::BlendMode::Opaque, "builtin:unlit"));
    AddQuadAt(test, 1.0f, Blended(test, Fenrir::BlendMode::Additive));
    AddQuadAt(test, -2.0f, Blended(test, Fenrir::BlendMode::Opaque));
    AddQuadAt(test, -3.0f, Blended(test, Fenrir::BlendMode::Masked, "builtin:unlit"));
    // The fallback material's item too.
    test.AddMeshRenderer(test.Mesh(), {});

    test.Frame();
    const std::vector<RecordedDraw> prepass = test.Draws("DepthPrepass");
    const std::vector<RecordedDraw> opaque = test.Draws("Opaque");
    const std::vector<RecordedDraw> transparent = test.Draws("Transparent");

    INFO("the five opaque and masked items draw in Opaque, the two blended ones in Transparent");
    REQUIRE(opaque.size() == 5);
    REQUIRE(transparent.size() == 2);
    INFO("the prepass draws the same items as Opaque, in the same order");
    REQUIRE(Items(prepass) == Items(opaque));
    INFO("each with the same mesh");
    for (std::size_t i = 0; i < prepass.size(); ++i)
    {
        REQUIRE(prepass[i].indexBuffer == opaque[i].indexBuffer);
        REQUIRE(prepass[i].draw.indexCount == opaque[i].draw.indexCount);
    }
    INFO("and none of the transparent items");
    const std::vector<std::uint32_t> prepassed = Items(prepass);
    for (const RecordedDraw& draw : transparent)
    {
        REQUIRE(std::ranges::find(prepassed, draw.draw.firstInstance) == prepassed.end());
    }
    INFO("DepthPrepass runs after the sky's lookup table, before Opaque");
    REQUIRE(RendererTest::PassesIn(RendererProbe::Submitted(test.renderer)).at(1) == "DepthPrepass");
    REQUIRE(RendererTest::PassesIn(RendererProbe::Submitted(test.renderer)).at(2) == "Opaque");
    INFO("nothing was logged as an error");
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("The prepass lays depth down and Opaque tests it for equality without writing it", "[renderer][prepass]")
{
    RendererTestApp test;
    test.AddCamera();
    AddQuadAt(test, 0.0f, Blended(test, Fenrir::BlendMode::Opaque));
    AddQuadAt(test, -1.0f, Blended(test, Fenrir::BlendMode::Masked));

    test.Frame();
    const std::vector<RecordedDraw> prepass = test.Draws("DepthPrepass");
    const std::vector<RecordedDraw> opaque = test.Draws("Opaque");

    REQUIRE(prepass.size() == 2);
    REQUIRE(opaque.size() == 2);
    for (const RecordedDraw& draw : prepass)
    {
        const RendererProbe::PipelineState state = RendererProbe::State(test.Device(), draw.pipeline);
        INFO("a prepass draw runs a DepthPrepass pipeline, testing and writing depth, greater-or-equal");
        REQUIRE(RendererProbe::Label(test.Device(), draw.pipeline) == "DepthPrepass");
        REQUIRE(state.depth.test);
        REQUIRE(state.depth.write);
        REQUIRE(state.depth.compare == CompareOp::GreaterEqual);
    }
    for (const RecordedDraw& draw : opaque)
    {
        const RendererProbe::PipelineState state = RendererProbe::State(test.Device(), draw.pipeline);
        INFO("an Opaque draw runs the material's own pipeline, testing for equality, writing none");
        REQUIRE(RendererProbe::Label(test.Device(), draw.pipeline) == "BuiltinLit");
        REQUIRE(state.depth.test);
        REQUIRE_FALSE(state.depth.write);
        REQUIRE(state.depth.compare == CompareOp::Equal);
    }
}

TEST_CASE("The prepass clears SceneDepth and an RG16F normal target at the view's size, which Opaque loads",
          "[renderer][prepass]")
{
    RendererTestApp test;
    test.AddCamera();
    AddQuadAt(test, 0.0f, test.Material());

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    const std::vector<PassRecord> prepass = RendererTest::Labelled(passes, "DepthPrepass");
    const std::vector<PassRecord> opaque = RendererTest::Labelled(passes, "Opaque");

    REQUIRE(prepass.size() == 1);
    REQUIRE(opaque.size() == 1);
    INFO("the prepass draws into one colour target, the normals, cleared to 0");
    REQUIRE(prepass[0].pass.colour.size() == 1);
    const Fenrir::Graphics::TextureDesc normals = RendererProbe::Desc(test.Device(), prepass[0].pass.colour[0].texture);
    REQUIRE(prepass[0].pass.colour[0].load == LoadOp::Clear);
    REQUIRE(prepass[0].pass.colour[0].clear == std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});
    INFO("RG16F, of the SceneColor's size and samples");
    const Fenrir::Graphics::TextureDesc colour =
        RendererProbe::Desc(test.Device(), opaque[0].pass.colour.at(0).texture);
    REQUIRE(normals.format == TextureFormat::Rg16Float);
    REQUIRE(normals.width == colour.width);
    REQUIRE(normals.height == colour.height);
    REQUIRE(normals.samples == colour.samples);
    INFO("it clears SceneDepth to the far plane, and Opaque loads that depth");
    REQUIRE(prepass[0].pass.depth.has_value());
    REQUIRE(opaque[0].pass.depth.has_value());
    REQUIRE(prepass[0].pass.depth->load == LoadOp::Clear);
    REQUIRE(prepass[0].pass.depth->clear == 0.0f);
    REQUIRE(opaque[0].pass.depth->texture == prepass[0].pass.depth->texture);
    REQUIRE(opaque[0].pass.depth->load == LoadOp::Load);
}

TEST_CASE("A masked item's prepass draw cuts it out by its base colour alpha, alpha cutoff and base colour texture",
          "[renderer][prepass]")
{
    RendererTestApp test;
    test.AddCamera();
    const auto masked = Blended(test, Fenrir::BlendMode::Masked);
    test.Store().SetAlphaCutoff(masked, 0.3f);
    test.Store().SetColour(masked, "baseColour", Fenrir::Colour{1.0f, 1.0f, 1.0f, 0.75f});
    AddQuadAt(test, 0.0f, masked);
    AddQuadAt(test, -1.0f, test.Material());

    test.Frame();
    const std::vector<RecordedDraw> prepass = test.Draws("DepthPrepass");

    REQUIRE(prepass.size() == 2);
    std::vector<std::string> groups;
    for (const RecordedDraw& draw : prepass)
    {
        groups.push_back(test.GroupLabel(draw, 2));
    }
    const auto maskedDraw = std::ranges::find(groups, "Prepass mask");
    INFO("the masked item binds its prepass group at set 2, the opaque one its material group");
    REQUIRE(maskedDraw != groups.end());
    REQUIRE(std::ranges::count(groups, "Material") == 1);
    const RecordedDraw& draw = prepass.at(static_cast<std::size_t>(maskedDraw - groups.begin()));
    INFO("its pipeline runs the ALPHA_MASK variant");
    REQUIRE(RendererProbe::Keys(test.Device(), draw.pipeline) == std::vector<std::string>{"ALPHA_MASK"});
    INFO("its block holds the base colour's alpha, 0.75, and the cutoff, 0.3");
    const std::vector<std::byte> block = test.MaterialBlock(draw);
    REQUIRE(std::abs(RendererTest::FloatAt(block, 0) - 0.75f) < 1e-6f);
    REQUIRE(std::abs(RendererTest::FloatAt(block, 4) - 0.3f) < 1e-6f);
    INFO("and it samples the base colour texture's default, white");
    REQUIRE(RendererProbe::Label(test.Device(), test.MaterialBinding(draw, 1).texture) == "Default white");
}

TEST_CASE("A masked user shader with no base colour draws in Opaque writing its depth, not in the prepass",
          "[renderer][prepass]")
{
    RendererTestApp test;
    test.AddCamera();
    const auto plain = test.Store().CreateMaterial(UserShader(test), Fenrir::AssetOwner::App());
    test.Store().SetBlendMode(plain, Fenrir::BlendMode::Masked);
    AddQuadAt(test, 0.0f, plain);
    AddQuadAt(test, -1.0f, test.Material());

    test.Frame();
    const std::vector<RecordedDraw> prepass = test.Draws("DepthPrepass");
    const std::vector<RecordedDraw> opaque = test.Draws("Opaque");

    INFO("only the lit item is in the prepass; both draw in Opaque");
    REQUIRE(prepass.size() == 1);
    REQUIRE(opaque.size() == 2);
    std::size_t writing = 0;
    for (const RecordedDraw& draw : opaque)
    {
        const RendererProbe::PipelineState state = RendererProbe::State(test.Device(), draw.pipeline);
        if (state.depth.write)
        {
            ++writing;
            INFO("the one not in the prepass tests and writes depth, greater-or-equal, as before the prepass");
            REQUIRE(state.depth.compare == CompareOp::GreaterEqual);
            REQUIRE(draw.draw.firstInstance != prepass[0].draw.firstInstance);
        }
    }
    REQUIRE(writing == 1);
}

TEST_CASE("A skinned item's prepass draw reads the compute-skinned vertices, or with compute skinning off runs its "
          "SKINNED vertex stage",
          "[renderer][prepass]")
{
    RendererTest::SkinningTestApp test;
    test.AddCamera();
    test.AddRig();

    SECTION("compute skinning on (ADR-0050)")
    {
        test.Frame();
        const std::vector<RecordedDraw> prepass = test.Draws("DepthPrepass");

        REQUIRE(prepass.size() == 1);
        INFO("drawn rigidly, its positions from the skinned vertex buffer");
        REQUIRE(RendererProbe::Keys(test.Device(), prepass[0].pipeline).empty());
        REQUIRE(RendererProbe::Label(test.Device(), prepass[0].vertexBuffers.at(0)) == "Skinned vertices");
        REQUIRE(test.GroupLabel(prepass[0], 3) == "Draw");
    }
    SECTION("compute skinning off")
    {
        test.renderer.SetComputeSkinning(false);
        test.Frame();
        const std::vector<RecordedDraw> prepass = test.Draws("DepthPrepass");

        REQUIRE(prepass.size() == 1);
        REQUIRE(RendererProbe::Keys(test.Device(), prepass[0].pipeline) == std::vector<std::string>{"SKINNED"});
        REQUIRE(test.GroupLabel(prepass[0], 3) == "Draw");
    }
}
