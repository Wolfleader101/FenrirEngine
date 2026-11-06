#pragma once

// The tonemapping golden scene (ADR-0049), shared by the GL tests of the Output pass: its tonemappers and grade
// (Tonemapping.cpp) and its camera effects (CameraEffects.cpp).

#include <cmath>
#include <cstdint>
#include <functional>

#include "GlScene.hpp"
#include "GoldenImages.hpp"

namespace GlTest
{
    /** @brief The golden scene's size. */
    constexpr std::uint32_t GoldenWidth = 96;
    constexpr std::uint32_t GoldenHeight = 64;

    /** @brief An entity at `position`, scaled by `scale`, drawing `mesh` with `material`. */
    inline void Place(GlScene& test, Fenrir::AssetHandle<Fenrir::Mesh> mesh,
                      Fenrir::AssetHandle<Fenrir::Material> material, Fenrir::Math::Vec3 position, float scale)
    {
        Fenrir::Entity entity = test.app.GetActiveScene().GetEntityList().CreateEntity();
        Fenrir::Transform& transform = entity.GetComponent<Fenrir::Transform>();
        transform.pos = position;
        transform.scale = Fenrir::Math::Vec3(scale);
        entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{mesh, material, true});
    }

    /** @brief A material on `shader` with base colour `colour`. */
    inline Fenrir::AssetHandle<Fenrir::Material> Material(GlScene& test, const char* shader, Fenrir::Colour colour)
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
     * stop apart. Behind: a dark blue. `setUp`, when given, changes the camera's other settings, such as its grade.
     */
    inline RendererTest::Rgba8Image DrawGoldenScene(Fenrir::Tonemapper tonemapper, float exposure,
                                                    const std::function<void(Fenrir::Camera&)>& setUp = {})
    {
        GlScene test;
        test.RequireOpenGL();
        Fenrir::AssetStore& store = test.assets.Store();
        Fenrir::ImageData data;
        data.width = GoldenWidth;
        data.height = GoldenHeight;
        const auto image = store.Add<Fenrir::Image>(std::move(data), Fenrir::AssetOwner::App());
        Fenrir::Camera& camera = test.AddCamera();
        camera.target = Fenrir::ImageTarget{image};
        camera.background = Fenrir::ColourBackground{Fenrir::Colour{0.02f, 0.03f, 0.06f, 1.0f}};
        camera.tonemapper = tonemapper;
        camera.exposure = exposure;
        if (setUp)
        {
            setUp(camera);
        }

        // The sun travels down, back and to the right, from over the camera's left shoulder.
        Fenrir::Entity sun = test.app.GetActiveScene().GetEntityList().CreateEntity();
        sun.GetComponent<Fenrir::Transform>().rot =
            Fenrir::Math::AngleAxis(Fenrir::Math::DegToRad(-30.0f), Fenrir::Math::Vec3(0.0f, 1.0f, 0.0f)) *
            Fenrir::Math::AngleAxis(Fenrir::Math::DegToRad(-40.0f), Fenrir::Math::Vec3(1.0f, 0.0f, 0.0f));
        sun.AddComponent<Fenrir::DirectionalLight>(Fenrir::DirectionalLight{Fenrir::Colour::White, 4.0f});
        test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::ColourSky{Fenrir::Colour{0.1f, 0.1f, 0.1f, 1.0f}});

        const auto sphere = store.Add<Fenrir::Mesh>(RendererTest::Sphere(), Fenrir::AssetOwner::App());
        const auto red = Material(test, "builtin:lit", Fenrir::Colour{0.8f, 0.05f, 0.05f, 1.0f});
        store.SetFloat(red, "roughness", 0.3f);
        const auto white = Material(test, "builtin:lit", Fenrir::Colour{0.9f, 0.9f, 0.9f, 1.0f});
        store.SetFloat(white, "roughness", 0.6f);
        const auto gold = Material(test, "builtin:lit", Fenrir::Colour{1.0f, 0.78f, 0.34f, 1.0f});
        store.SetFloat(gold, "metallic", 1.0f);
        store.SetFloat(gold, "roughness", 0.25f);
        Place(test, sphere, red, Fenrir::Math::Vec3(-1.6f, 0.5f, -4.0f), 1.3f);
        Place(test, sphere, white, Fenrir::Math::Vec3(0.0f, 0.5f, -4.0f), 1.3f);
        Place(test, sphere, gold, Fenrir::Math::Vec3(1.6f, 0.5f, -4.0f), 1.3f);

        const auto patch = store.Add<Fenrir::Mesh>(RendererTest::Quad(0.8f), Fenrir::AssetOwner::App());
        for (int stop = 0; stop < 6; ++stop)
        {
            const float scale = std::exp2(static_cast<float>(stop - 2));
            const auto orange =
                Material(test, "builtin:unlit", Fenrir::Colour{1.0f * scale, 0.45f * scale, 0.1f * scale, 1.0f});
            Place(test, patch, orange, Fenrir::Math::Vec3(-2.5f + static_cast<float>(stop), -1.35f, -4.0f), 1.0f);
        }

        test.RunFrame();
        INFO("no GL error or other error was logged");
        REQUIRE(test.messages->errors.empty());
        return test.ReadTarget(image);
    }

} // namespace GlTest
