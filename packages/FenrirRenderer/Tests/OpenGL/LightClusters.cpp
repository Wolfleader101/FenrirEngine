// Light clustering on OpenGL (the spec's seam 2; ADR-0049): builtin:light-clusters listing a view's lights in its
// clusters, and Fenrir/Lighting.glsl looping over its pixel's, read back from the view's SceneColor. Clustering only
// skips lights that cannot reach a pixel, so a wall lit by many small point, spot and rect lights is lit the same,
// pixel for pixel, with it on and off; and the cluster heat view shows how many lights each pixel loops over.

#include <cmath>
#include <cstdint>
#include <random>

#include <catch2/catch_test_macros.hpp>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirMath/Math.hpp"
#include "GlScene.hpp"

using Fenrir::GlProbe;
using Fenrir::Math::Vec3;
using GlTest::GlScene;

namespace
{
    constexpr std::uint32_t Size = GlScene::Size;
    /** @brief How far the wall is in front of the camera. */
    constexpr float WallDistance = 6.0f;

    /**
     * @brief A white rough wall filling the view under a black sky, lit by 150 small lights in front of it, a third
     * each point, spot and rect lights, some casting shadows, each reaching 0.2 to 0.9 units: a few at each pixel, none
     * at some. Returns its camera.
     */
    Fenrir::Camera& AddLitWall(GlScene& test)
    {
        Fenrir::Camera& camera = test.AddCamera();
        // The plain colour, so the probe compares the lights' sums rather than the tonemap's.
        camera.look.tonemapper = Fenrir::Tonemapper::None;
        Fenrir::Scene& scene = test.app.GetActiveScene();
        scene.AddResource<Fenrir::Sky>(Fenrir::ColourSky{{0.0f, 0.0f, 0.0f, 1.0f}});
        const auto material = test.AddQuad(20.0f, WallDistance, "builtin:lit", Fenrir::Colour::White);
        test.assets.Store().SetFloat(material, "roughness", 0.6f);

        std::mt19937 random(300);
        std::uniform_real_distribution<float> across(-3.5f, 3.5f);
        std::uniform_real_distribution<float> height(0.2f, 1.2f);
        std::uniform_real_distribution<float> range(0.2f, 0.9f);
        std::uniform_real_distribution<float> hue(0.2f, 1.0f);
        for (int i = 0; i < 150; ++i)
        {
            Fenrir::Entity entity = scene.GetEntityList().CreateEntity();
            entity.GetComponent<Fenrir::Transform>().pos = {across(random), across(random),
                                                            -WallDistance + height(random)};
            const Fenrir::Colour colour{hue(random), hue(random), hue(random), 1.0f};
            // Each faces the wall: its -Z is the camera's. The first four point lights and eight spot lights cast
            // shadows, the default budgets, so the shadowed records' indices come through the clusters too.
            switch (i % 3)
            {
            case 0:
                entity.AddComponent<Fenrir::PointLight>(Fenrir::PointLight{colour, 0.5f, range(random), i < 12});
                break;
            case 1:
                entity.AddComponent<Fenrir::SpotLight>(
                    Fenrir::SpotLight{colour, 0.5f, range(random), 0.3f, 0.6f, i < 24});
                break;
            default: {
                Fenrir::RectLight rect;
                rect.colour = colour;
                rect.intensity = 0.5f;
                rect.width = 0.3f;
                rect.height = 0.2f;
                rect.range = range(random);
                entity.AddComponent<Fenrir::RectLight>(rect);
                break;
            }
            }
        }
        return camera;
    }
} // namespace

TEST_CASE("A wall lit by many lights is lit the same with its lights clustered and not", "[renderer][gl][lights]")
{
    GlScene test;
    test.RequireOpenGL();
    AddLitWall(test);

    const GlProbe::Image clustered = test.Frame();
    test.renderer.SetClusteredLights(false);
    const GlProbe::Image unclustered = test.Frame();

    INFO("both frames read back at the window's size");
    REQUIRE(clustered.width == unclustered.width);
    std::uint32_t lit = 0;
    std::uint32_t dark = 0;
    for (std::uint32_t y = 0; y < Size; ++y)
    {
        for (std::uint32_t x = 0; x < Size; ++x)
        {
            const GlProbe::Pixel a = clustered.At(x, y);
            const GlProbe::Pixel b = unclustered.At(x, y);
            for (int c = 0; c < 3; ++c)
            {
                CAPTURE(x, y, c, a[c], b[c]);
                // Only the order of the sum can differ: the lights each pixel adds are the same.
                INFO("the pixel's channel is the same clustered as unclustered");
                REQUIRE(std::abs(a[c] - b[c]) <= 1e-3f * std::max(1.0f, b[c]));
            }
            const bool any = b[0] > 0.0f || b[1] > 0.0f || b[2] > 0.0f;
            (any ? lit : dark) += 1;
        }
    }
    INFO("the wall is lit at most pixels and dark at some, so both are compared: lit " << lit << ", dark " << dark);
    REQUIRE(lit > Size * Size / 2);
    REQUIRE(dark > 0);
    INFO("with no GL error");
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("The cluster heat view colours every pixel a light reaches, and few of them red", "[renderer][gl][lights]")
{
    GlScene test;
    test.RequireOpenGL();
    Fenrir::Camera& camera = AddLitWall(test);

    const GlProbe::Image lit = test.Frame();
    camera.debugView = Fenrir::CameraDebugView::LightClusters;
    const GlProbe::Image heat = test.Frame();

    std::uint32_t coloured = 0;
    std::uint32_t red = 0;
    for (std::uint32_t y = 0; y < Size; ++y)
    {
        for (std::uint32_t x = 0; x < Size; ++x)
        {
            const GlProbe::Pixel pixel = heat.At(x, y);
            const bool any = pixel[0] > 0.0f || pixel[1] > 0.0f || pixel[2] > 0.0f;
            coloured += any ? 1 : 0;
            red += pixel[0] == 1.0f && pixel[1] == 0.0f && pixel[2] == 0.0f ? 1 : 0;
            const GlProbe::Pixel shaded = lit.At(x, y);
            if (shaded[0] > 0.0f || shaded[1] > 0.0f || shaded[2] > 0.0f)
            {
                CAPTURE(x, y);
                INFO("a pixel some light reaches loops over at least that light");
                REQUIRE(any);
            }
        }
    }
    INFO("and its clusters list far fewer than all 150 lights, so it is red, at 256, at few pixels: " << red << " of "
                                                                                                      << coloured);
    REQUIRE(coloured > 0);
    REQUIRE(red < coloured / 4);
    INFO("with no GL error");
    REQUIRE(test.messages->errors.empty());
}
