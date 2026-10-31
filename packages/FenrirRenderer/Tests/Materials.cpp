// Materials on the null backend through App::RunFrame (the spec's seam 1, ADR-0024): blend modes and their render
// stages and state, the ALPHA_MASK variant, textures with their defaults, samplers and mips, and a user's own shader.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "RendererTestApp.hpp"

using Fenrir::RendererProbe;
using Fenrir::Graphics::BlendMode;
using Fenrir::Graphics::CullMode;
using RendererTest::RecordedDraw;
using RendererTest::RendererTestApp;

namespace
{
    /** @brief A quad at depth `z` drawn with `material`, facing the camera at z = 5. */
    Fenrir::Entity AddQuadAt(RendererTestApp& test, float z, Fenrir::AssetHandle<Fenrir::Material> material)
    {
        return test.AddMeshRenderer(test.Mesh(RendererTest::TexturedQuad(1.0f, z)), material);
    }

    /** @brief A material on builtin:unlit with `mode`. */
    Fenrir::AssetHandle<Fenrir::Material> Blended(RendererTestApp& test, Fenrir::BlendMode mode)
    {
        const auto material = test.Material("builtin:unlit");
        test.Store().SetBlendMode(material, mode);
        return material;
    }

    /** @brief The label of the texture a draw bound at set 2 binding `binding`. */
    std::string TextureLabel(const RendererTestApp& test, const RecordedDraw& draw, std::uint32_t binding)
    {
        return RendererProbe::Label(test.Device(), test.MaterialBinding(draw, binding).texture);
    }
} // namespace

TEST_CASE("A masked material draws with its shader's ALPHA_MASK variant and its alpha cutoff", "[renderer][materials]")
{
    RendererTestApp test;
    test.AddCamera();
    const auto masked = test.Material("builtin:lit");
    test.Store().SetBlendMode(masked, Fenrir::BlendMode::Masked);
    test.Store().SetAlphaCutoff(masked, 0.3f);
    AddQuadAt(test, 0.0f, masked);
    AddQuadAt(test, -1.0f, test.Material("builtin:lit"));

    test.Frame();
    const std::vector<RecordedDraw> draws = test.Draws();

    INFO("both draw in the Opaque pass");
    REQUIRE(draws.size() == 2);
    std::vector<std::vector<std::string>> keys;
    for (const RecordedDraw& draw : draws)
    {
        keys.push_back(RendererProbe::Keys(test.Device(), draw.pipeline));
    }
    const auto maskedDraw = std::find(keys.begin(), keys.end(), std::vector<std::string>{"ALPHA_MASK"});
    INFO("one draws with the ALPHA_MASK variant, the other with the base variant");
    REQUIRE(maskedDraw != keys.end());
    REQUIRE(std::count(keys.begin(), keys.end(), std::vector<std::string>{}) == 1);
    // builtin:lit's ALPHA_MASK block, std140: baseColour 0, emissive 16, metallic 32, roughness 36, emissiveStrength
    // 40, alphaCutoff 44.
    const std::vector<std::byte> block =
        test.MaterialBlock(draws.at(static_cast<std::size_t>(maskedDraw - keys.begin())));
    INFO("the masked material's block holds its alpha cutoff, 0.3");
    REQUIRE(std::abs(RendererTest::FloatAt(block, 44) - 0.3f) < 1e-6f);
}

TEST_CASE("Opaque and masked items write depth; alpha-blended and additive ones do not, and blend",
          "[renderer][materials]")
{
    RendererTestApp test;
    test.AddCamera();
    AddQuadAt(test, 0.0f, Blended(test, Fenrir::BlendMode::Opaque));
    AddQuadAt(test, 0.0f, Blended(test, Fenrir::BlendMode::Masked));
    AddQuadAt(test, 0.0f, Blended(test, Fenrir::BlendMode::AlphaBlend));
    AddQuadAt(test, 1.0f, Blended(test, Fenrir::BlendMode::Additive));

    test.Frame();
    const std::vector<RecordedDraw> opaque = test.Draws("Opaque");
    const std::vector<RecordedDraw> transparent = test.Draws("Transparent");

    INFO("the opaque and masked items draw in Opaque, the blended ones in Transparent");
    REQUIRE(opaque.size() == 2);
    REQUIRE(transparent.size() == 2);
    for (const RecordedDraw& draw : opaque)
    {
        const RendererProbe::PipelineState state = RendererProbe::State(test.Device(), draw.pipeline);
        INFO("an Opaque item tests and writes depth, greater-or-equal, without blending");
        REQUIRE(state.depth.test);
        REQUIRE(state.depth.write);
        REQUIRE(state.depth.compare == Fenrir::Graphics::CompareOp::GreaterEqual);
        REQUIRE(state.blend == BlendMode::None);
    }
    std::vector<BlendMode> blends;
    for (const RecordedDraw& draw : transparent)
    {
        const RendererProbe::PipelineState state = RendererProbe::State(test.Device(), draw.pipeline);
        INFO("a Transparent item tests depth, greater-or-equal, without writing it");
        REQUIRE(state.depth.test);
        REQUIRE_FALSE(state.depth.write);
        REQUIRE(state.depth.compare == Fenrir::Graphics::CompareOp::GreaterEqual);
        blends.push_back(state.blend);
    }
    INFO("the alpha-blended item blends by alpha, the additive one adds");
    REQUIRE(std::count(blends.begin(), blends.end(), BlendMode::Alpha) == 1);
    REQUIRE(std::count(blends.begin(), blends.end(), BlendMode::Additive) == 1);
    INFO("Transparent runs after Background, before Output");
    REQUIRE(RendererTest::PassesIn(RendererProbe::Submitted(test.renderer)) ==
            std::vector<std::string>{"BrdfLut", "Opaque", "Background", "Transparent", "Output"});
    INFO("nothing was logged as an error");
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Transparent items draw back to front by the view-space depth of their origins", "[renderer][materials]")
{
    RendererTestApp test;
    // Two views from opposite sides: each draws the items in its own back-to-front order.
    test.AddCamera({0.0f, 0.0f, 10.0f});
    Fenrir::Entity behind = test.AddCamera({0.0f, 0.0f, -10.0f});
    behind.GetComponent<Fenrir::Transform>().rot = Fenrir::Math::AngleAxis(3.14159265f, Fenrir::Math::Vec3(0, 1, 0));
    behind.GetComponent<Fenrir::Camera>().order = 1;
    // Spawned in neither order.
    for (const float z : {-2.0f, 3.0f, 0.0f, -5.0f, 1.0f})
    {
        Fenrir::Entity entity =
            test.AddMeshRenderer(test.Mesh(RendererTest::TexturedQuad()), Blended(test, Fenrir::BlendMode::AlphaBlend));
        entity.GetComponent<Fenrir::Transform>().pos = {0.0f, 0.0f, z};
    }

    test.Frame();
    const std::vector<RecordedDraw> draws = test.Draws("Transparent");

    INFO("each view draws the five items");
    REQUIRE(draws.size() == 10);
    std::vector<float> first;
    std::vector<float> second;
    for (std::size_t i = 0; i < draws.size(); ++i)
    {
        (i < 5 ? first : second).push_back(test.DrawPosition(draws[i]).z);
    }
    INFO("the camera at +10 looking along -Z draws the farthest (most negative z) first");
    REQUIRE(first == std::vector<float>{-5.0f, -2.0f, 0.0f, 1.0f, 3.0f});
    INFO("the camera at -10 looking along +Z draws them in the opposite order");
    REQUIRE(second == std::vector<float>{3.0f, 1.0f, 0.0f, -2.0f, -5.0f});
}

TEST_CASE("A double-sided material draws with culling off", "[renderer][materials]")
{
    RendererTestApp test;
    test.AddCamera();
    const auto twoSided = test.Material("builtin:lit");
    test.Store().SetDoubleSided(twoSided, true);
    AddQuadAt(test, 0.0f, twoSided);
    AddQuadAt(test, -1.0f, test.Material("builtin:lit"));

    test.Frame();
    const std::vector<RecordedDraw> draws = test.Draws();

    REQUIRE(draws.size() == 2);
    std::vector<CullMode> culls;
    for (const RecordedDraw& draw : draws)
    {
        culls.push_back(RendererProbe::State(test.Device(), draw.pipeline).cull);
    }
    INFO("the double-sided item culls nothing; the single-sided one culls back faces");
    REQUIRE(std::count(culls.begin(), culls.end(), CullMode::None) == 1);
    REQUIRE(std::count(culls.begin(), culls.end(), CullMode::Back) == 1);
}

TEST_CASE("A texture parameter left unset binds its default texture, and a set one its image with mips",
          "[renderer][materials]")
{
    RendererTestApp test;
    test.AddCamera();
    const auto material = test.Material("builtin:lit");
    // 4 x 2: mips of 4 x 2, 2 x 1 and 1 x 1.
    const auto image = test.Image(RendererTest::Picture(4, 2, std::vector<std::uint8_t>(32, 200)));
    REQUIRE(test.Store().SetTexture(material, "baseColourTexture", image));
    AddQuadAt(test, 0.0f, material);

    test.Frame();
    const std::vector<RecordedDraw> draws = test.Draws();

    REQUIRE(draws.size() == 1);
    // builtin:lit's set 2: MaterialParams at 0, then baseColourTexture, metallicRoughnessTexture, normalTexture,
    // emissiveTexture and occlusionTexture at 1 to 5, each defaulting by pragma to white, white, normal, white and
    // white.
    INFO("the unset textures bind their defaults: white, the flat normal, white and white");
    REQUIRE(TextureLabel(test, draws[0], 2) == "Default white");
    REQUIRE(TextureLabel(test, draws[0], 3) == "Default normal");
    REQUIRE(TextureLabel(test, draws[0], 4) == "Default white");
    REQUIRE(TextureLabel(test, draws[0], 5) == "Default white");
    INFO("and no variant key is added for them");
    REQUIRE(RendererProbe::Keys(test.Device(), draws[0].pipeline).empty());
    const Fenrir::Graphics::TextureHandle uploaded = test.MaterialBinding(draws[0], 1).texture;
    const Fenrir::Graphics::TextureDesc desc = RendererProbe::Desc(test.Device(), uploaded);
    INFO("the set texture binds the image, uploaded as a 4 x 2 sRGB texture with three mip levels");
    REQUIRE(desc.width == 4);
    REQUIRE(desc.height == 2);
    REQUIRE(desc.format == Fenrir::Graphics::TextureFormat::Rgba8Srgb);
    REQUIRE(desc.mipLevels == 3);
    INFO("every mip level was written at upload");
    REQUIRE(RendererProbe::WrittenMips(test.Device(), uploaded) == std::vector<bool>{true, true, true});
}

TEST_CASE("A linear image is uploaded as a linear texture", "[renderer][materials]")
{
    RendererTestApp test;
    test.AddCamera();
    const auto material = test.Material("builtin:lit");
    const auto image = test.Image(RendererTest::Picture(1, 1, {128, 128, 255, 255}, Fenrir::ColourSpace::Linear));
    REQUIRE(test.Store().SetTexture(material, "normalTexture", image));
    AddQuadAt(test, 0.0f, material);

    test.Frame();
    const std::vector<RecordedDraw> draws = test.Draws();

    REQUIRE(draws.size() == 1);
    INFO("the normal texture is RGBA8 without the sRGB curve, one mip level");
    const Fenrir::Graphics::TextureDesc desc =
        RendererProbe::Desc(test.Device(), test.MaterialBinding(draws[0], 3).texture);
    REQUIRE(desc.format == Fenrir::Graphics::TextureFormat::Rgba8Unorm);
    REQUIRE(desc.mipLevels == 1);
}

TEST_CASE("Two texture parameters with equal sampler descriptions share one sampler", "[renderer][materials]")
{
    RendererTestApp test;
    test.AddCamera();
    const auto material = test.Material("builtin:lit");
    const auto image = test.Image(RendererTest::Picture(1, 1, {255, 255, 255, 255}));
    Fenrir::SamplerDescription clamped;
    clamped.wrapU = Fenrir::SamplerWrap::ClampToEdge;
    clamped.wrapV = Fenrir::SamplerWrap::ClampToEdge;
    clamped.filter = Fenrir::SamplerFilter::Bilinear;
    Fenrir::SamplerDescription nearest = clamped;
    nearest.filter = Fenrir::SamplerFilter::Nearest;
    REQUIRE(test.Store().SetTexture(material, "baseColourTexture", image, clamped));
    REQUIRE(test.Store().SetTexture(material, "emissiveTexture", image, clamped));
    REQUIRE(test.Store().SetTexture(material, "metallicRoughnessTexture", image, nearest));
    AddQuadAt(test, 0.0f, material);

    test.Frame();
    const std::vector<RecordedDraw> draws = test.Draws();

    REQUIRE(draws.size() == 1);
    const Fenrir::Graphics::SamplerHandle base = test.MaterialBinding(draws[0], 1).sampler;
    const Fenrir::Graphics::SamplerHandle emissive = test.MaterialBinding(draws[0], 4).sampler;
    const Fenrir::Graphics::SamplerHandle metallic = test.MaterialBinding(draws[0], 2).sampler;
    INFO("the two clamped bilinear parameters share one sampler");
    REQUIRE(base == emissive);
    INFO("the nearest one has a sampler of its own");
    REQUIRE(base != metallic);
}

TEST_CASE("A user shader container loaded as ShaderData draws with its own set 2 laid out from reflection",
          "[renderer][materials]")
{
    RendererTestApp test;
    test.AddCamera();
    // The container file fenrir_add_shaders wrote, read as a game reads its own shaders into ShaderData.
    std::ifstream file(FENRIR_TEST_USER_SHADER, std::ios::binary);
    const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    INFO("the container file was written");
    REQUIRE_FALSE(bytes.empty());
    Fenrir::ShaderData data;
    for (const char byte : bytes)
    {
        data.bytes.push_back(static_cast<std::byte>(byte));
    }
    const auto shader = test.Store().Add<Fenrir::Shader>(std::move(data), Fenrir::AssetOwner::App());
    INFO("the store takes it as a shader");
    REQUIRE_FALSE(shader.IsEmpty());
    const auto material = test.Store().CreateMaterial(shader, Fenrir::AssetOwner::App());
    REQUIRE(test.Store().SetFloat(material, "strength", 3.0f));
    AddQuadAt(test, 0.0f, material);

    test.Frame();
    const std::vector<RecordedDraw> draws = test.Draws();

    INFO("it is drawn with its own program, not the fallback");
    REQUIRE(draws.size() == 1);
    REQUIRE(RendererProbe::Label(test.Device(), draws[0].pipeline) == "UserTint");
    INFO("its own key FANCY stays off");
    REQUIRE(RendererProbe::Keys(test.Device(), draws[0].pipeline).empty());
    const std::vector<std::byte> block = test.MaterialBlock(draws[0]);
    INFO("its block holds tint's default (0.25, 0.5, 0.75, 1) at 0, and the override strength 3 at 16");
    REQUIRE(std::abs(RendererTest::FloatAt(block, 0) - 0.25f) < 1e-6f);
    REQUIRE(std::abs(RendererTest::FloatAt(block, 4) - 0.5f) < 1e-6f);
    REQUIRE(std::abs(RendererTest::FloatAt(block, 8) - 0.75f) < 1e-6f);
    REQUIRE(std::abs(RendererTest::FloatAt(block, 16) - 3.0f) < 1e-6f);
    INFO("its texture pattern, at binding 1, binds its default, black");
    REQUIRE(TextureLabel(test, draws[0], 1) == "Default black");
    INFO("nothing was logged as an error");
    REQUIRE(test.messages->errors.empty());
}
