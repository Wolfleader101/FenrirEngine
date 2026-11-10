// Reflection inputs on OpenGL (ADR-0056, lighting ticket 29): pixel probes of the textures a reflecting camera keeps
// across frames. Its history's level 0 holds SceneColor as it was before bloom, each pixel the mean of its samples
// under MSAA; its Hi-Z pyramid's levels hold the nearest depth of the pixels they cover, so a small near square shows
// in every level that covers it. The camera is orthographic, one world unit a pixel of the 64 x 64 window.

#include <cmath>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "GlScene.hpp"

using Fenrir::GlProbe;
using GlTest::GlScene;

namespace
{
    constexpr std::uint32_t Size = GlScene::Size;
    /** @brief The orthographic camera's near and far planes. */
    constexpr float Near = 0.1f;
    constexpr float Far = 100.0f;

    /** @brief A reflecting orthographic camera one world unit a pixel, on black, with bloom at `bloom`. */
    Fenrir::Camera& AddCamera(GlScene& test, float bloom)
    {
        Fenrir::Camera& camera = test.AddCamera();
        camera.projection = Fenrir::Orthographic{static_cast<float>(Size), Near, Far};
        camera.background = Fenrir::ColourBackground{Fenrir::Colour::Black};
        camera.look.bloom.intensity = bloom;
        camera.reflections.enabled = true;
        return camera;
    }

    /** @brief The reversed-Z depth the orthographic camera gives a point `distance` in front of it. */
    float DepthAt(float distance)
    {
        return (Far - distance) / (Far - Near);
    }

    /** @brief The only reflecting camera's history, read back at level 0. */
    GlProbe::Image History(GlScene& test)
    {
        const auto kept = Fenrir::RendererProbe::Reflections(test.renderer);
        REQUIRE(kept.size() == 1);
        return GlProbe::Read(Fenrir::RendererProbe::Device(test.renderer), kept[0].history);
    }

    bool Close(float a, float b, float tolerance)
    {
        return std::abs(a - b) <= tolerance;
    }
} // namespace

TEST_CASE("GL: a reflecting camera's history holds its SceneColor from before bloom", "[renderer][gl][ssr]")
{
    GlScene test;
    test.RequireOpenGL();
    AddCamera(test, 0.5f);
    // A bright unlit square on the centre 2 x 2 pixels, which half of bloom's blur spreads thin.
    constexpr float value = 4.0f;
    test.AddQuad(2.0f, 1.0f, "builtin:unlit", Fenrir::Colour{value, value, value, 1.0f});

    const GlProbe::Image bloomed = test.Frame(Size, Size);
    const GlProbe::Image history = History(test);
    REQUIRE((history.width == Size && history.height == Size));
    INFO("the square's pixel holds its own colour, not its bloom: " << history.At(31, 31)[0] << " against SceneColor's "
                                                                    << bloomed.At(31, 31)[0]);
    REQUIRE(Close(history.At(31, 31)[0], value, 4e-3f));
    REQUIRE(bloomed.At(31, 31)[0] < 0.75f * value);
    INFO("and the black around it is black, where SceneColor holds the glow");
    REQUIRE(history.At(28, 31)[0] == 0.0f);
    REQUIRE(bloomed.At(28, 31)[0] > 0.0f);
}

TEST_CASE("GL: under MSAA a reflecting camera's history holds the mean of each pixel's samples", "[renderer][gl][ssr]")
{
    GlScene test(4);
    test.RequireOpenGL();
    AddCamera(test, 0.0f);
    // A square 3 pixels across, so its left and right edges cut pixels 30 and 33 in half.
    constexpr float value = 1.0f;
    test.AddQuad(3.0f, 1.0f, "builtin:unlit", Fenrir::Colour{value, value, value, 1.0f});

    test.RunFrame();
    const GlProbe::Image history = History(test);
    INFO("a covered pixel holds the colour");
    REQUIRE(Close(history.At(31, 31)[0], value, 2e-3f));
    INFO("a pixel the edge halves holds some of its samples' colour, not all or none: " << history.At(30, 31)[0]);
    REQUIRE(history.At(30, 31)[0] > 0.2f * value);
    REQUIRE(history.At(30, 31)[0] < 0.8f * value);
}

TEST_CASE("GL: each level of a reflecting camera's Hi-Z pyramid holds the nearest depth it covers",
          "[renderer][gl][ssr]")
{
    GlScene test;
    test.RequireOpenGL();
    AddCamera(test, 0.0f);
    // A wall at distance 10 behind everything, and a 2 x 2 pixel square at distance 1 on the centre.
    test.AddQuad(2.0f * static_cast<float>(Size), 10.0f, "builtin:unlit", Fenrir::Colour{0.1f, 0.1f, 0.1f, 1.0f});
    test.AddQuad(2.0f, 1.0f, "builtin:unlit", Fenrir::Colour{1.0f, 1.0f, 1.0f, 1.0f});

    test.RunFrame();
    const auto kept = Fenrir::RendererProbe::Reflections(test.renderer);
    REQUIRE(kept.size() == 1);
    Fenrir::Graphics::GraphicsDevice& device = Fenrir::RendererProbe::Device(test.renderer);
    const auto level = [&](std::uint32_t mip) { return GlProbe::Read(device, kept[0].pyramid, mip); };
    const float near = DepthAt(1.0f);
    const float far = DepthAt(10.0f);

    const GlProbe::Image full = level(0);
    INFO("level 0 is SceneDepth: the square's depth on it, the wall's elsewhere");
    REQUIRE(Close(full.At(31, 31)[0], near, 1e-5f));
    REQUIRE(Close(full.At(30, 30)[0], far, 1e-5f));

    const GlProbe::Image half = level(1);
    REQUIRE((half.width == Size / 2 && half.height == Size / 2));
    INFO("level 1's texel over pixels 30 and 31, the wall and the square, takes the square's");
    REQUIRE(Close(half.At(15, 15)[0], near, 1e-5f));
    REQUIRE(Close(half.At(16, 16)[0], near, 1e-5f));
    INFO("and one over the wall alone the wall's");
    REQUIRE(Close(half.At(14, 15)[0], far, 1e-5f));

    const GlProbe::Image last = level(6);
    INFO("the last level, 1 x 1, covers everything: the square's");
    REQUIRE((last.width == 1 && last.height == 1));
    REQUIRE(Close(last.At(0, 0)[0], near, 1e-5f));
}

TEST_CASE("GL: a Hi-Z level of an odd size folds the last row and column of the one above into its last texel",
          "[renderer][gl][ssr]")
{
    GlScene test;
    test.RequireOpenGL();
    // A 48 x 48 viewport, one unit a pixel: its pyramid runs 48, 24, 12, 6, 3 and 1, so the last level comes from an
    // odd one, whose third column and row it sees only by folding them in.
    Fenrir::Camera& camera = AddCamera(test, 0.0f);
    camera.viewport = {0.0f, 0.0f, 0.75f, 0.75f};
    camera.projection = Fenrir::Orthographic{48.0f, Near, Far};
    test.AddQuad(128.0f, 10.0f, "builtin:unlit", Fenrir::Colour{0.1f, 0.1f, 0.1f, 1.0f});
    // A 4 x 4 pixel square at distance 1 on pixels 40 to 43 each way, inside level 4's last texel alone.
    Fenrir::AssetStore& store = test.assets.Store();
    const auto material = store.CreateMaterial(
        store.Load<Fenrir::Shader>("builtin:unlit", Fenrir::AssetOwner::App()).handle, Fenrir::AssetOwner::App());
    const auto mesh = store.Add<Fenrir::Mesh>(RendererTest::Quad(4.0f, -1.0f), Fenrir::AssetOwner::App());
    Fenrir::Entity square = test.app.GetActiveScene().GetEntityList().CreateEntity();
    square.GetComponent<Fenrir::Transform>().pos = {18.0f, -18.0f, 0.0f};
    square.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{mesh, material, true});

    test.RunFrame();
    const auto kept = Fenrir::RendererProbe::Reflections(test.renderer);
    REQUIRE(kept.size() == 1);
    Fenrir::Graphics::GraphicsDevice& device = Fenrir::RendererProbe::Device(test.renderer);
    const float near = DepthAt(1.0f);
    const float far = DepthAt(10.0f);

    const GlProbe::Image full = GlProbe::Read(device, kept[0].pyramid, 0);
    REQUIRE((full.width == 48 && full.height == 48));
    INFO("the square is where it was placed");
    REQUIRE(Close(full.At(41, 41)[0], near, 1e-5f));
    REQUIRE(Close(full.At(38, 41)[0], far, 1e-5f));

    const GlProbe::Image three = GlProbe::Read(device, kept[0].pyramid, 4);
    REQUIRE((three.width == 3 && three.height == 3));
    INFO("level 4, 3 x 3, has the square in its last texel only");
    REQUIRE(Close(three.At(2, 2)[0], near, 1e-5f));
    REQUIRE(Close(three.At(1, 1)[0], far, 1e-5f));

    const GlProbe::Image last = GlProbe::Read(device, kept[0].pyramid, 5);
    INFO("level 5, 1 x 1, folds in level 4's third column and row, so it has the square's depth");
    REQUIRE((last.width == 1 && last.height == 1));
    REQUIRE(Close(last.At(0, 0)[0], near, 1e-5f));
}
