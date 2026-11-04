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
        camera.bloom.intensity = bloom;
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
