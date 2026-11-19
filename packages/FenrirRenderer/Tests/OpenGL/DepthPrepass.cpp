// The depth prepass on OpenGL (the spec's seam 2, ADR-0049): its normal target holds the view-space normal builtin:lit
// shades with, and a masked quad's cut-out pixels are not in the depth, so what is behind them is shaded. Each test
// reads back the view's SceneColor or its normal target, unpacked by Core/OctahedralNormals.

#include <cmath>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "Core/OctahedralNormals.hpp"
#include "GlScene.hpp"

using Fenrir::GlProbe;
using Fenrir::Math::Vec3;
using GlTest::GlScene;

namespace
{
    constexpr std::uint32_t Size = GlScene::Size;

    /** @brief A material on `shader` with `mode` and `colour`. */
    Fenrir::AssetHandle<Fenrir::Material> MaterialOn(GlScene& test, const char* shader, Fenrir::BlendMode mode,
                                                     Fenrir::Colour colour)
    {
        Fenrir::AssetStore& store = test.assets.Store();
        const auto material = store.CreateMaterial(store.Load<Fenrir::Shader>(shader, Fenrir::AssetOwner::App()).handle,
                                                   Fenrir::AssetOwner::App());
        store.SetBlendMode(material, mode);
        store.SetColour(material, "baseColour", colour);
        return material;
    }

    /** @brief A square of side `size` with UV0 at `position`, facing +Z turned `yaw` radians about +Y. */
    void AddQuad(GlScene& test, float size, Vec3 position, float yaw, Fenrir::AssetHandle<Fenrir::Material> material)
    {
        const auto mesh =
            test.assets.Store().Add<Fenrir::Mesh>(RendererTest::TexturedQuad(size, 0.0f), Fenrir::AssetOwner::App());
        Fenrir::Entity entity = test.app.GetActiveScene().GetEntityList().CreateEntity();
        Fenrir::Transform& transform = entity.GetComponent<Fenrir::Transform>();
        transform.pos = position;
        transform.rot = glm::angleAxis(yaw, Vec3(0.0f, 1.0f, 0.0f));
        entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{mesh, material, true});
    }

    /** @brief The last frame's normal target, read back. */
    GlProbe::Image ReadNormals(GlScene& test)
    {
        const Fenrir::Graphics::TextureHandle normals = Fenrir::RendererProbe::SceneNormals(test.renderer);
        REQUIRE_FALSE(normals.IsEmpty());
        return GlProbe::Read(Fenrir::RendererProbe::Device(test.renderer), normals);
    }

    /** @brief The view-space normal the normal target holds at pixel (`x`, `y`). */
    Vec3 NormalAt(const GlProbe::Image& normals, std::uint32_t x, std::uint32_t y)
    {
        const GlProbe::Pixel texel = normals.At(x, y);
        return Fenrir::Render::OctahedralDecode({texel[0], texel[1]});
    }
} // namespace

TEST_CASE("The normal target holds the view-space normal builtin:lit shades with", "[renderer][gl][prepass]")
{
    // The quad's turn and the camera's: the normal in view space is the quad's turned back by the camera's.
    const float quadYaw = GENERATE(0.0f, 0.5f, -0.7f);
    const float cameraYaw = GENERATE(0.0f, 0.4f);
    GlScene test;
    test.RequireOpenGL();
    // GlScene's camera, turned.
    Fenrir::Entity camera = test.app.GetActiveScene().GetEntityList().CreateEntity();
    camera.AddComponent<Fenrir::Camera>().look.bloom.intensity = 0.0f;
    camera.GetComponent<Fenrir::Transform>().rot = glm::angleAxis(cameraYaw, Vec3(0.0f, 1.0f, 0.0f));
    // In front of the turned camera, at distance 2 along its -Z.
    const Vec3 forward(-std::sin(cameraYaw), 0.0f, -std::cos(cameraYaw));
    AddQuad(test, 1.0f, forward * 2.0f, quadYaw,
            MaterialOn(test, "builtin:lit", Fenrir::BlendMode::Opaque, Fenrir::Colour::White));

    test.RunFrame();
    const GlProbe::Image normals = ReadNormals(test);

    const float relative = quadYaw - cameraYaw;
    const Vec3 expected(std::sin(relative), 0.0f, std::cos(relative));
    INFO("quad turned " << quadYaw << ", camera turned " << cameraYaw << ": the centre pixel's normal is ("
                        << NormalAt(normals, Size / 2, Size / 2).x << ", " << NormalAt(normals, Size / 2, Size / 2).y
                        << ", " << NormalAt(normals, Size / 2, Size / 2).z << ")");
    REQUIRE(glm::length(NormalAt(normals, Size / 2, Size / 2) - expected) < 0.01f);
    INFO("a pixel no item covers keeps the cleared value, (0, 0)");
    REQUIRE(GlProbe::Near(normals.At(1, 1), {0.0f, 0.0f, 0.0f, 1.0f}));
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A double-sided quad's back face has its normal turned over in the normal target", "[renderer][gl][prepass]")
{
    GlScene test;
    test.RequireOpenGL();
    test.AddCamera();
    const auto material = MaterialOn(test, "builtin:lit", Fenrir::BlendMode::Opaque, Fenrir::Colour::White);
    test.assets.Store().SetDoubleSided(material, true);
    // Facing away from the camera, turned a little more than half a turn.
    AddQuad(test, 1.0f, {0.0f, 0.0f, -2.0f}, 3.4f, material);

    test.RunFrame();
    const GlProbe::Image normals = ReadNormals(test);

    const Vec3 expected(-std::sin(3.4f), 0.0f, -std::cos(3.4f));
    REQUIRE(glm::length(NormalAt(normals, Size / 2, Size / 2) - expected) < 0.01f);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A masked quad's cut-out pixels are not in the depth: the quad behind is shaded there",
          "[renderer][gl][prepass]")
{
    GlScene test;
    test.RequireOpenGL();
    test.AddCamera();
    const auto material = MaterialOn(test, "builtin:unlit", Fenrir::BlendMode::Masked, Fenrir::Colour::White);
    // Two texels: the left one transparent white, the right one opaque green, read nearest so the edge stays sharp.
    const auto image = test.assets.Store().Add<Fenrir::Image>(
        RendererTest::Picture(2, 1, {255, 255, 255, 0, 0, 255, 0, 255}), Fenrir::AssetOwner::App());
    Fenrir::SamplerDescription nearest;
    nearest.filter = Fenrir::SamplerFilter::Nearest;
    nearest.wrapU = Fenrir::SamplerWrap::ClampToEdge;
    nearest.wrapV = Fenrir::SamplerWrap::ClampToEdge;
    REQUIRE(test.assets.Store().SetTexture(material, "baseColourTexture", image, nearest));
    // The masked quad spans about pixels 18 to 46 across, its left half 18 to 32. Behind it, a red quad filling the
    // view, turned 0.5 radians so its normal tells it apart.
    AddQuad(test, 1.0f, {0.0f, 0.0f, -2.0f}, 0.0f, material);
    AddQuad(test, 8.0f, {0.0f, 0.0f, -4.0f}, 0.5f,
            MaterialOn(test, "builtin:unlit", Fenrir::BlendMode::Opaque, Fenrir::Colour::Red));

    const GlProbe::Image frame = test.Frame();
    const GlProbe::Image normals = ReadNormals(test);

    INFO("a pixel of the cut-out left half shows the red quad behind");
    REQUIRE(GlProbe::Near(frame.At(25, Size / 2), {1.0f, 0.0f, 0.0f, 1.0f}));
    INFO("and holds its normal, so the prepass laid down the red quad's depth there, not the masked one's");
    REQUIRE(glm::length(NormalAt(normals, 25, Size / 2) - Vec3(std::sin(0.5f), 0.0f, std::cos(0.5f))) < 0.01f);
    INFO("a pixel of the right half is the masked quad's opaque green, facing the camera");
    REQUIRE(GlProbe::Near(frame.At(39, Size / 2), {0.0f, 1.0f, 0.0f, 1.0f}));
    REQUIRE(glm::length(NormalAt(normals, 39, Size / 2) - Vec3(0.0f, 0.0f, 1.0f)) < 0.01f);
    REQUIRE(test.messages->errors.empty());
}
