// The Output pass's exposure and tonemappers on OpenGL (ADR-0049): flat colours through each tonemapper held to the C++
// mirror in Core/Tonemapping.hpp, and golden images: one lit scene with highlights well over 1, drawn into an image
// target through each tonemapper and at three exposures, and compared with the committed PNG in OpenGL/goldens under
// RendererTest::GoldenTolerance. Run the GL tests with FENRIR_UPDATE_GOLDENS=1 to write the goldens again, after a
// change meant to alter them; look at each before committing it.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "Core/Tonemapping.hpp"
#include "GlScene.hpp"
#include "GoldenImages.hpp"

using Fenrir::GlProbe;
using Fenrir::RendererProbe;
using Fenrir::Math::Vec3;
using GlTest::GlScene;

namespace
{
    constexpr std::uint32_t Width = 96;
    constexpr std::uint32_t Height = 64;

    /** @brief A sphere of radius 0.5 about the origin, 24 segments round and 16 pole to pole, counter-clockwise. */
    Fenrir::MeshData Sphere()
    {
        constexpr std::uint32_t segments = 24;
        constexpr std::uint32_t rings = 16;
        Fenrir::MeshData mesh;
        for (std::uint32_t ring = 0; ring <= rings; ++ring)
        {
            const float polar = std::numbers::pi_v<float> * static_cast<float>(ring) / rings;
            for (std::uint32_t segment = 0; segment <= segments; ++segment)
            {
                const float azimuth = 2.0f * std::numbers::pi_v<float> * static_cast<float>(segment) / segments;
                const Vec3 normal(std::sin(polar) * std::cos(azimuth), std::cos(polar),
                                  -std::sin(polar) * std::sin(azimuth));
                mesh.positions.push_back(normal * 0.5f);
                mesh.normals.push_back(normal);
            }
        }
        for (std::uint32_t ring = 0; ring < rings; ++ring)
        {
            for (std::uint32_t segment = 0; segment < segments; ++segment)
            {
                const std::uint32_t a = ring * (segments + 1) + segment;
                const std::uint32_t b = a + segments + 1;
                for (const std::uint32_t index : {a, b, a + 1, a + 1, b, b + 1})
                {
                    mesh.indices.push_back(index);
                }
            }
        }
        return mesh;
    }

    /** @brief An entity at `position`, scaled by `scale`, drawing `mesh` with `material`. */
    void Place(GlScene& test, Fenrir::AssetHandle<Fenrir::Mesh> mesh, Fenrir::AssetHandle<Fenrir::Material> material,
               Vec3 position, float scale)
    {
        Fenrir::Entity entity = test.app.GetActiveScene().GetEntityList().CreateEntity();
        Fenrir::Transform& transform = entity.GetComponent<Fenrir::Transform>();
        transform.pos = position;
        transform.scale = Vec3(scale);
        entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{mesh, material, true});
    }

    /** @brief A material on `shader` with base colour `colour`. */
    Fenrir::AssetHandle<Fenrir::Material> Material(GlScene& test, const char* shader, Fenrir::Colour colour)
    {
        Fenrir::AssetStore& store = test.assets.Store();
        const auto material = store.CreateMaterial(store.Load<Fenrir::Shader>(shader, Fenrir::AssetOwner::App()).handle,
                                                   Fenrir::AssetOwner::App());
        store.SetColour(material, "baseColour", colour);
        return material;
    }

    /**
     * @brief The golden scene, drawn by one camera through `tonemapper` at `exposure` EV into a 96 x 64 image, read
     * back as it stores it. Above: a red dielectric, a white one and a gold metal sphere under a sun of intensity 4,
     * whose highlights reach well over 1. Below: unlit orange patches from a quarter to eight times (1, 0.45, 0.1), one
     * stop apart. Behind: a dark blue.
     */
    RendererTest::Rgba8Image DrawGoldenScene(Fenrir::Tonemapper tonemapper, float exposure)
    {
        GlScene test;
        test.RequireOpenGL();
        Fenrir::AssetStore& store = test.assets.Store();
        Fenrir::ImageData data;
        data.width = Width;
        data.height = Height;
        const auto image = store.Add<Fenrir::Image>(std::move(data), Fenrir::AssetOwner::App());
        Fenrir::Camera& camera = test.AddCamera();
        camera.target = Fenrir::ImageTarget{image};
        camera.background = Fenrir::ColourBackground{Fenrir::Colour{0.02f, 0.03f, 0.06f, 1.0f}};
        camera.tonemapper = tonemapper;
        camera.exposure = exposure;

        // The sun travels down, back and to the right, from over the camera's left shoulder.
        Fenrir::Entity sun = test.app.GetActiveScene().GetEntityList().CreateEntity();
        sun.GetComponent<Fenrir::Transform>().rot =
            Fenrir::Math::AngleAxis(Fenrir::Math::DegToRad(-30.0f), Vec3(0.0f, 1.0f, 0.0f)) *
            Fenrir::Math::AngleAxis(Fenrir::Math::DegToRad(-40.0f), Vec3(1.0f, 0.0f, 0.0f));
        sun.AddComponent<Fenrir::DirectionalLight>(Fenrir::DirectionalLight{Fenrir::Colour::White, 4.0f});
        test.app.GetActiveScene().AddResource<Fenrir::AmbientLight>(
            Fenrir::AmbientLight{Fenrir::Colour{0.1f, 0.1f, 0.1f, 1.0f}, 1.0f});

        const auto sphere = store.Add<Fenrir::Mesh>(Sphere(), Fenrir::AssetOwner::App());
        const auto red = Material(test, "builtin:lit", Fenrir::Colour{0.8f, 0.05f, 0.05f, 1.0f});
        store.SetFloat(red, "roughness", 0.3f);
        const auto white = Material(test, "builtin:lit", Fenrir::Colour{0.9f, 0.9f, 0.9f, 1.0f});
        store.SetFloat(white, "roughness", 0.6f);
        const auto gold = Material(test, "builtin:lit", Fenrir::Colour{1.0f, 0.78f, 0.34f, 1.0f});
        store.SetFloat(gold, "metallic", 1.0f);
        store.SetFloat(gold, "roughness", 0.25f);
        Place(test, sphere, red, Vec3(-1.6f, 0.5f, -4.0f), 1.3f);
        Place(test, sphere, white, Vec3(0.0f, 0.5f, -4.0f), 1.3f);
        Place(test, sphere, gold, Vec3(1.6f, 0.5f, -4.0f), 1.3f);

        const auto patch = store.Add<Fenrir::Mesh>(RendererTest::Quad(0.8f), Fenrir::AssetOwner::App());
        for (int stop = 0; stop < 6; ++stop)
        {
            const float scale = std::exp2(static_cast<float>(stop - 2));
            const auto orange =
                Material(test, "builtin:unlit", Fenrir::Colour{1.0f * scale, 0.45f * scale, 0.1f * scale, 1.0f});
            Place(test, patch, orange, Vec3(-2.5f + static_cast<float>(stop), -1.35f, -4.0f), 1.0f);
        }

        test.RunFrame();
        const Fenrir::Graphics::TextureHandle texture = RendererProbe::ImageTexture(test.renderer, image);
        REQUIRE_FALSE(texture.IsEmpty());
        const GlProbe::Image pixels = GlProbe::Read(RendererProbe::Device(test.renderer), texture);
        INFO("no GL error or other error was logged");
        REQUIRE(test.messages->errors.empty());

        RendererTest::Rgba8Image frame;
        frame.width = pixels.width;
        frame.height = pixels.height;
        frame.rgba.reserve(pixels.rgba.size());
        for (const float value : pixels.rgba)
        {
            frame.rgba.push_back(static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)));
        }
        return frame;
    }

    void RequireGolden(std::string_view name, const RendererTest::Rgba8Image& frame)
    {
        RendererTest::RequireGolden(name, frame, FENRIR_TEST_GOLDENS, FENRIR_TEST_GOLDEN_FAILURES);
    }
} // namespace

TEST_CASE("The lit scene through None matches its golden image, its highlights clipped", "[renderer][gl][golden]")
{
    RequireGolden("tonemap-none", DrawGoldenScene(Fenrir::Tonemapper::None, 0.0f));
}

TEST_CASE("The lit scene through AgX matches its golden image", "[renderer][gl][golden]")
{
    RequireGolden("tonemap-agx", DrawGoldenScene(Fenrir::Tonemapper::AgX, 0.0f));
}

TEST_CASE("The lit scene through PBR Neutral matches its golden image", "[renderer][gl][golden]")
{
    RequireGolden("tonemap-pbr-neutral", DrawGoldenScene(Fenrir::Tonemapper::PbrNeutral, 0.0f));
}

TEST_CASE("The lit scene through ACES matches its golden image", "[renderer][gl][golden]")
{
    RequireGolden("tonemap-aces", DrawGoldenScene(Fenrir::Tonemapper::Aces, 0.0f));
}

TEST_CASE("The lit scene through AgX two stops under matches its golden image", "[renderer][gl][golden]")
{
    RequireGolden("tonemap-agx-minus-2ev", DrawGoldenScene(Fenrir::Tonemapper::AgX, -2.0f));
}

TEST_CASE("The lit scene through AgX two stops over matches its golden image", "[renderer][gl][golden]")
{
    RequireGolden("tonemap-agx-plus-2ev", DrawGoldenScene(Fenrir::Tonemapper::AgX, 2.0f));
}

TEST_CASE("Output's tonemappers and exposure match their C++ mirror", "[renderer][gl][tonemapping]")
{
    // A view of nothing but its background: one flat linear colour through Output, read back as the sRGB image
    // stores it, against the mirror's colour encoded the same way.
    constexpr std::array<Fenrir::Tonemapper, 4> tonemappers{Fenrir::Tonemapper::None, Fenrir::Tonemapper::AgX,
                                                            Fenrir::Tonemapper::PbrNeutral, Fenrir::Tonemapper::Aces};
    const std::array<Vec3, 4> colours{Vec3(0.18f), Vec3(1.0f), Vec3(4.0f, 1.0f, 0.25f), Vec3(0.05f, 0.4f, 1.5f)};
    constexpr std::array<float, 3> exposures{0.0f, -1.5f, 2.0f};
    const auto encode = [](float linear) {
        return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    };
    for (const Fenrir::Tonemapper tonemapper : tonemappers)
    {
        for (const Vec3 colour : colours)
        {
            for (const float exposure : exposures)
            {
                GlScene test;
                test.RequireOpenGL();
                Fenrir::ImageData data;
                data.width = 4;
                data.height = 4;
                const auto image = test.assets.Store().Add<Fenrir::Image>(std::move(data), Fenrir::AssetOwner::App());
                Fenrir::Camera& camera = test.AddCamera();
                camera.target = Fenrir::ImageTarget{image};
                camera.background = Fenrir::ColourBackground{Fenrir::Colour{colour.r, colour.g, colour.b, 1.0f}};
                camera.tonemapper = tonemapper;
                camera.exposure = exposure;

                test.RunFrame();
                const GlProbe::Image pixels = GlProbe::Read(RendererProbe::Device(test.renderer),
                                                            RendererProbe::ImageTexture(test.renderer, image));

                const Vec3 expected = Fenrir::Render::Tonemap(static_cast<Fenrir::Render::Tonemapper>(tonemapper),
                                                              colour * Fenrir::Render::ExposureScale(exposure));
                const GlProbe::Pixel actual = pixels.At(2, 2);
                INFO("tonemapper " << static_cast<int>(tonemapper) << ", colour (" << colour.r << ", " << colour.g
                                   << ", " << colour.b << ") at " << exposure << " EV: the GPU stored (" << actual[0]
                                   << ", " << actual[1] << ", " << actual[2] << "), the mirror encodes to ("
                                   << encode(expected.r) << ", " << encode(expected.g) << ", " << encode(expected.b)
                                   << ")");
                REQUIRE(GlProbe::Near(actual, {encode(expected.r), encode(expected.g), encode(expected.b), 1.0f}));
            }
        }
    }
}

TEST_CASE("Each tonemapper draws the lit scene differently", "[renderer][gl][golden]")
{
    // Against the golden comparison itself, so a tonemapper silently drawing as another fails here too.
    const RendererTest::Rgba8Image none = DrawGoldenScene(Fenrir::Tonemapper::None, 0.0f);
    const RendererTest::Rgba8Image agx = DrawGoldenScene(Fenrir::Tonemapper::AgX, 0.0f);
    const RendererTest::Rgba8Image neutral = DrawGoldenScene(Fenrir::Tonemapper::PbrNeutral, 0.0f);
    const RendererTest::Rgba8Image aces = DrawGoldenScene(Fenrir::Tonemapper::Aces, 0.0f);
    const RendererTest::Rgba8Image brighter = DrawGoldenScene(Fenrir::Tonemapper::AgX, 2.0f);
    INFO("no two of None, AgX, PBR Neutral, ACES and AgX at +2 EV match");
    const RendererTest::Rgba8Image* frames[]{&none, &agx, &neutral, &aces, &brighter};
    for (std::size_t i = 0; i < std::size(frames); ++i)
    {
        for (std::size_t j = i + 1; j < std::size(frames); ++j)
        {
            INFO("frames " << i << " and " << j);
            REQUIRE_FALSE(RendererTest::CompareWithGolden(*frames[i], *frames[j]).Matches({}));
        }
    }
}
