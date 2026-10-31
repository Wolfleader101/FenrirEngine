// Materials on OpenGL (the spec's seam 2, ADR-0024): masked cut-outs, alpha blending and additive blending over opaque
// items, in one view and in several, an emissive colour with no emissive texture and scaled by its emissive strength,
// and a material set of a block and
// four textures bound at the GL units the flattening formula gives. Each test reads back a view's SceneColor, linear.

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirShaders/GlFourTextures.hpp"
#include "GlScene.hpp"

using Fenrir::GlProbe;
using GlTest::GlScene;

namespace
{
    constexpr std::uint32_t Size = GlScene::Size;
    // The Background pass's colour in a scene with no Sky, linear.
    constexpr GlProbe::Pixel Background{0.015f, 0.015f, 0.015f, 1.0f};

    /** @brief A material on builtin:unlit with `mode` and `colour`. */
    Fenrir::AssetHandle<Fenrir::Material> Unlit(GlScene& test, Fenrir::BlendMode mode, Fenrir::Colour colour)
    {
        Fenrir::AssetStore& store = test.assets.Store();
        const auto material = store.CreateMaterial(
            store.Load<Fenrir::Shader>("builtin:unlit", Fenrir::AssetOwner::App()).handle, Fenrir::AssetOwner::App());
        store.SetBlendMode(material, mode);
        store.SetColour(material, "baseColour", colour);
        return material;
    }

    /** @brief A square of side `size` with UV0, facing the cameras at the origin from `distance` along -Z. */
    void AddTexturedQuad(GlScene& test, float size, float distance, Fenrir::AssetHandle<Fenrir::Material> material)
    {
        const auto mesh = test.assets.Store().Add<Fenrir::Mesh>(RendererTest::TexturedQuad(size, -distance),
                                                                Fenrir::AssetOwner::App());
        Fenrir::Entity entity = test.app.GetActiveScene().GetEntityList().CreateEntity();
        entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{mesh, material, true});
    }

    /** @brief A linear 1 x 1 image of one grey level, red to alpha all `value` (0 to 255). */
    Fenrir::AssetHandle<Fenrir::Image> Grey(GlScene& test, std::uint8_t value)
    {
        return test.assets.Store().Add<Fenrir::Image>(
            RendererTest::Picture(1, 1, {value, value, value, value}, Fenrir::ColourSpace::Linear),
            Fenrir::AssetOwner::App());
    }
} // namespace

TEST_CASE("A masked quad's cut-out pixels show the background, the rest its colour", "[renderer][gl][materials]")
{
    GlScene test;
    test.RequireOpenGL();
    test.AddCamera();
    const auto material = Unlit(test, Fenrir::BlendMode::Masked, Fenrir::Colour::White);
    // Two texels: the left one transparent white, the right one opaque green, read nearest so the edge stays sharp.
    const auto image = test.assets.Store().Add<Fenrir::Image>(
        RendererTest::Picture(2, 1, {255, 255, 255, 0, 0, 255, 0, 255}), Fenrir::AssetOwner::App());
    Fenrir::SamplerDescription nearest;
    nearest.filter = Fenrir::SamplerFilter::Nearest;
    nearest.wrapU = Fenrir::SamplerWrap::ClampToEdge;
    nearest.wrapV = Fenrir::SamplerWrap::ClampToEdge;
    REQUIRE(test.assets.Store().SetTexture(material, "baseColourTexture", image, nearest));
    // The quad spans about pixels 18 to 46 across: its left half from 18 to 32, its right half from 32 to 46.
    AddTexturedQuad(test, 1.0f, 2.0f, material);

    const GlProbe::Image frame = test.Frame();

    INFO("a pixel of the left half, alpha 0 under the cutoff of 0.5, is discarded: the background shows");
    REQUIRE(GlProbe::Near(frame.At(25, Size / 2), Background));
    INFO("a pixel of the right half is the opaque green");
    REQUIRE(GlProbe::Near(frame.At(39, Size / 2), {0.0f, 1.0f, 0.0f, 1.0f}));
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("An alpha-blended quad over an opaque one gives the blended colour", "[renderer][gl][materials]")
{
    GlScene test;
    test.RequireOpenGL();
    test.AddCamera();
    // Spawned nearer first, so drawing in spawn order would hide the far quad behind the blended one's depth.
    AddTexturedQuad(test, 1.0f, 2.0f, Unlit(test, Fenrir::BlendMode::AlphaBlend, {0.0f, 1.0f, 0.0f, 0.25f}));
    AddTexturedQuad(test, 4.0f, 4.0f, Unlit(test, Fenrir::BlendMode::Opaque, Fenrir::Colour::Red));

    const GlProbe::Image frame = test.Frame();

    // Straight alpha: green x 0.25 + red x 0.75; alpha 0.25 + 1 x 0.75.
    INFO("the centre is 0.25 of the green over 0.75 of the red");
    REQUIRE(GlProbe::Near(frame.At(Size / 2, Size / 2), {0.75f, 0.25f, 0.0f, 1.0f}, 0.01f));
    INFO("beside the blended quad, the red quad alone");
    REQUIRE(GlProbe::Near(frame.At(8, Size / 2), {1.0f, 0.0f, 0.0f, 1.0f}));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("An additive quad adds its colour to what is behind it", "[renderer][gl][materials]")
{
    GlScene test;
    test.RequireOpenGL();
    test.AddCamera();
    AddTexturedQuad(test, 1.0f, 2.0f, Unlit(test, Fenrir::BlendMode::Additive, {0.0f, 0.0f, 0.5f, 1.0f}));
    AddTexturedQuad(test, 4.0f, 4.0f, Unlit(test, Fenrir::BlendMode::Opaque, {0.5f, 0.0f, 0.0f, 1.0f}));

    const GlProbe::Image frame = test.Frame();

    INFO("the centre is the red plus the blue");
    REQUIRE(GlProbe::Near(frame.At(Size / 2, Size / 2), {0.5f, 0.0f, 0.5f, 1.0f}, 0.01f));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Two views each blend their transparent quad over their own opaque items, frame after frame",
          "[renderer][gl][materials]")
{
    GlScene test;
    test.RequireOpenGL();
    // A narrow view on the left and a wide one on the right, so their SceneColors differ in size.
    Fenrir::Camera& left = test.AddCamera();
    left.viewport = {0.0f, 0.0f, 0.25f, 1.0f};
    Fenrir::Camera& right = test.AddCamera();
    right.viewport = {0.25f, 0.0f, 0.75f, 1.0f};
    right.order = 1;
    AddTexturedQuad(test, 0.5f, 2.0f, Unlit(test, Fenrir::BlendMode::AlphaBlend, {0.0f, 1.0f, 0.0f, 0.5f}));
    AddTexturedQuad(test, 1.0f, 3.0f, Unlit(test, Fenrir::BlendMode::Opaque, Fenrir::Colour::Red));
    // A small nearer opaque quad just below the centre, in front of the blended one: the blended quad must not cover
    // it, so its depth test against the opaque items holds.
    Fenrir::AssetStore& store = test.assets.Store();
    Fenrir::MeshData blocker = RendererTest::TexturedQuad(0.25f, -1.5f);
    for (Fenrir::Math::Vec3& position : blocker.positions)
    {
        position.y -= 0.125f;
    }
    Fenrir::Entity front = test.app.GetActiveScene().GetEntityList().CreateEntity();
    front.AddComponent<Fenrir::MeshRenderer>(
        Fenrir::MeshRenderer{store.Add<Fenrir::Mesh>(std::move(blocker), Fenrir::AssetOwner::App()),
                             Unlit(test, Fenrir::BlendMode::Opaque, Fenrir::Colour::Blue), true});

    for (int frame = 0; frame < 8; ++frame)
    {
        test.RunFrame();
        for (const auto& [width, label] : {std::pair(Size / 4, "left"), std::pair(Size * 3 / 4, "right")})
        {
            const GlProbe::Image view = GlProbe::Read(Fenrir::RendererProbe::Device(test.renderer),
                                                      Fenrir::RendererProbe::SceneColour(test.renderer, width, Size));
            INFO("frame " << frame << ", the " << label << " view");
            INFO("at its centre, half the green over half the red");
            REQUIRE(GlProbe::Near(view.At(width / 2, Size / 2 - 2), {0.5f, 0.5f, 0.0f, 1.0f}, 0.01f));
            INFO("just below it, the nearer blue quad, which the blended quad behind it does not cover");
            REQUIRE(GlProbe::Near(view.At(width / 2, Size / 2 + 4), {0.0f, 0.0f, 1.0f, 1.0f}));
        }
    }
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A lit material's emissive colour shows with no emissive texture", "[renderer][gl][materials]")
{
    GlScene test;
    test.RequireOpenGL();
    test.AddCamera();
    // Black, so neither a light nor the default sky's dark grey adds more than its Fresnel's 4% of that grey: only the
    // emissive colour is left.
    const auto material = test.AddQuad(1.0f, 2.0f, "builtin:lit", Fenrir::Colour::Black);
    test.assets.Store().SetColour(material, "emissive", {1.0f, 0.5f, 0.25f, 1.0f});

    const GlProbe::Image frame = test.Frame();

    INFO("emissive x the default white emissive texture");
    REQUIRE(GlProbe::Near(frame.At(Size / 2, Size / 2), {1.0f, 0.5f, 0.25f, 1.0f}, 0.01f));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A lit material's emissive strength scales its emissive colour linearly", "[renderer][gl][materials]")
{
    GlScene test;
    test.RequireOpenGL();
    test.AddCamera();
    const auto material = test.AddQuad(1.0f, 2.0f, "builtin:lit", Fenrir::Colour::Black);
    Fenrir::AssetStore& store = test.assets.Store();
    store.SetColour(material, "emissive", {0.25f, 0.125f, 0.0625f, 1.0f});

    INFO("emissiveStrength defaults to 1: the emissive colour as it is");
    REQUIRE(GlProbe::Near(test.Frame().At(Size / 2, Size / 2), {0.25f, 0.125f, 0.0625f, 1.0f}, 0.01f));
    store.SetFloat(material, "emissiveStrength", 4.0f);
    INFO("at 4, four times it");
    REQUIRE(GlProbe::Near(test.Frame().At(Size / 2, Size / 2), {1.0f, 0.5f, 0.25f, 1.0f}, 0.01f));
    store.SetFloat(material, "emissiveStrength", 16.0f);
    INFO("at 16, sixteen times it, past 1 into HDR");
    REQUIRE(GlProbe::Near(test.Frame().At(Size / 2, Size / 2), {4.0f, 2.0f, 1.0f, 1.0f}, 0.02f));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A material set of a block and four textures binds each texture at its own GL unit",
          "[renderer][gl][materials]")
{
    GlScene test;
    test.RequireOpenGL();
    test.AddCamera();
    Fenrir::AssetStore& store = test.assets.Store();
    Fenrir::ShaderData data;
    const auto bytes = std::as_bytes(std::span(Fenrir::Shaders::GlFourTextures));
    data.bytes.assign(bytes.begin(), bytes.end());
    const auto material = store.CreateMaterial(store.Add<Fenrir::Shader>(std::move(data), Fenrir::AssetOwner::App()),
                                               Fenrir::AssetOwner::App());
    // 51, 102, 153 and 204 are 0.2, 0.4, 0.6 and 0.8.
    REQUIRE(store.SetTexture(material, "first", Grey(test, 51)));
    REQUIRE(store.SetTexture(material, "second", Grey(test, 102)));
    REQUIRE(store.SetTexture(material, "third", Grey(test, 153)));
    REQUIRE(store.SetTexture(material, "fourth", Grey(test, 204)));
    test.AddQuad(1.0f, 2.0f, material);

    const GlProbe::Image frame = test.Frame();

    INFO("each channel holds its own texture's value: 0.2, 0.4, 0.6 and 0.8");
    REQUIRE(GlProbe::Near(frame.At(Size / 2, Size / 2), {0.2f, 0.4f, 0.6f, 0.8f}));
    REQUIRE(test.messages->errors.empty());
}
