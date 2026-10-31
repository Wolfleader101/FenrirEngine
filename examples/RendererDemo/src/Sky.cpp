#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirECS/ComponentType.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirScene/Scene.hpp"
#include "RendererScenes.hpp"

namespace RendererDemo
{
    namespace
    {
        using Fenrir::Math::Vec3;

        constexpr float Pi = 3.14159265f;
        // The colour sky B switches to, and the colour O gives the camera's own background, both linear.
        constexpr Fenrir::Colour DuskColour{0.22f, 0.12f, 0.30f, 1.0f};
        constexpr Fenrir::Colour OverrideColour{0.05f, 0.35f, 0.32f, 1.0f};
        // The neutral grey Darcy Wilson's skybox is lit by (its SkyLight), linear: its teal sea and blue sky would tint
        // everything otherwise.
        constexpr Fenrir::Colour DarcyLight{0.35f, 0.35f, 0.35f, 1.0f};
        // What T and E step the camera through: its tonemapper, from the default, and its exposure in EV stops.
        struct NamedTonemapper
        {
            Fenrir::Tonemapper tonemapper;
            const char* name;
        };
        constexpr std::array<NamedTonemapper, 4> Tonemappers{{{Fenrir::Tonemapper::AgX, "AgX"},
                                                              {Fenrir::Tonemapper::PbrNeutral, "PBR Neutral"},
                                                              {Fenrir::Tonemapper::Aces, "ACES"},
                                                              {Fenrir::Tonemapper::None, "None (clamped)"}}};
        constexpr std::array<float, 4> Exposures{0.0f, 1.0f, 2.0f, -1.0f};

        /** @brief The scene's sky and camera, and what the keys have set, kept as a scene resource. */
        struct SkyState
        {
            /** @brief The generated skybox (examples/assets/skybox). */
            Fenrir::AssetHandle<Fenrir::Image> cubemap;
            /** @brief Darcy Wilson's skybox, the legacy DemoApp's (examples/assets/skybox-darcy), which C switches to.
             */
            Fenrir::AssetHandle<Fenrir::Image> darcyCubemap;
            /** @brief The free-fly camera: O overrides its background, T and E set its tonemapper and exposure. */
            Fenrir::Entity camera;
            Fenrir::Entity spinner;
            bool cubemapSky = true;
            bool darcySky = false;
            bool overridden = false;
            std::size_t tonemapper = 0;
            std::size_t exposure = 0;
            float seconds = 0.0f;
        };

        /** @brief The scene's Sky as B and C have set it: a skybox cubemap, generated or Darcy Wilson's, or one dusk
         * colour. */
        Fenrir::Sky SkyOf(const SkyState& state)
        {
            if (state.cubemapSky)
            {
                return Fenrir::CubemapSky{state.darcySky ? state.darcyCubemap : state.cubemap};
            }
            return Fenrir::ColourSky{DuskColour};
        }

        /**
         * @brief Gives the scene the Sky B and C have set, and lights it with a neutral grey SkyLight while Darcy
         * Wilson's skybox is shown, else with the Sky itself (ADR-0047).
         */
        void ApplySky(Fenrir::Scene& scene, const SkyState& state)
        {
            if (Fenrir::Sky* sky = scene.GetResource<Fenrir::Sky>())
            {
                *sky = SkyOf(state);
            }
            else
            {
                scene.AddResource<Fenrir::Sky>(SkyOf(state));
            }
            scene.RemoveResource<Fenrir::SkyLight>();
            if (state.cubemapSky && state.darcySky)
            {
                scene.AddResource<Fenrir::SkyLight>(Fenrir::SkyLight{Fenrir::ColourSky{DarcyLight}});
            }
        }

        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            SkyState state;
            // The skybox's six faces, loaded as one cubemap image from the shared assets (examples/assets/skybox).
            const Fenrir::LoadResult<Fenrir::Image> cubemap = store.Load<Fenrir::Image>("skybox/skybox.cubemap", scene);
            if (!cubemap.Ok())
            {
                app.Logger()->Error("Sky: the skybox did not load from the asset root");
            }
            state.cubemap = cubemap.handle;
            // Darcy Wilson's, which the legacy DemoApp drew, for C.
            const Fenrir::LoadResult<Fenrir::Image> darcy =
                store.Load<Fenrir::Image>("skybox-darcy/skybox.cubemap", scene);
            if (!darcy.Ok())
            {
                app.Logger()->Error("Sky: Darcy Wilson's skybox did not load from the asset root");
            }
            state.darcyCubemap = darcy.handle;

            // The free-fly camera is the scene's only camera when it is spawned; O overrides its background.
            Fenrir::EntityList& entities = scene.GetEntityList();
            const std::array<Fenrir::ComponentType, 1> cameras{Fenrir::ComponentType::Native<Fenrir::Camera>()};
            entities.ForEachWith(cameras, [&](std::uint32_t id) { state.camera = entities.GetEntity(id); });

            const Fenrir::LoadResult<Fenrir::Mesh> cube = store.Load<Fenrir::Mesh>("cube.obj#Mesh0", scene);
            const Fenrir::LoadResult<Fenrir::Mesh> sphere = store.Load<Fenrir::Mesh>("sphere.obj#Mesh0", scene);
            if (!cube.Ok() || !sphere.Ok())
            {
                app.Logger()->Error("Sky: the cube or sphere did not load from the asset root");
                return;
            }
            const auto lit = store.Load<Fenrir::Shader>("builtin:lit", scene).handle;
            const auto material = [&](std::uint32_t srgb, float metallic, float roughness) {
                const auto made = store.CreateMaterial(lit, scene);
                store.SetColour(made, "baseColour", Fenrir::Colour::FromSrgb(srgb));
                store.SetFloat(made, "metallic", metallic);
                store.SetFloat(made, "roughness", roughness);
                return made;
            };
            const auto add = [&](Fenrir::AssetHandle<Fenrir::Mesh> mesh, const Vec3& position, const Vec3& scale,
                                 Fenrir::AssetHandle<Fenrir::Material> paint) {
                Fenrir::Entity entity = entities.CreateEntity();
                auto& transform = entity.AddComponent<Fenrir::Transform>();
                transform.pos = position;
                transform.scale = scale;
                entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{mesh, paint});
                return entity;
            };

            // A small plinth with a spinning box on it and a ring of spheres, so the sky shows all round them.
            add(cube.handle, {0.0f, -0.25f, 0.0f}, {6.0f, 0.5f, 6.0f}, material(0x9A9488FF, 0.0f, 0.8f));
            state.spinner = add(cube.handle, {0.0f, 1.0f, 0.0f}, {1.2f, 1.2f, 1.2f}, material(0xE8E8E8FF, 0.0f, 0.4f));
            constexpr std::array<std::uint32_t, 5> colours{0xD04040FF, 0xE0A030FF, 0x40B040FF, 0x4070E0FF, 0x9050D0FF};
            for (std::size_t i = 0; i < colours.size(); ++i)
            {
                const float angle = 2.0f * Pi * static_cast<float>(i) / static_cast<float>(colours.size());
                add(sphere.handle, {2.2f * std::cos(angle), 0.5f, 2.2f * std::sin(angle)}, {0.8f, 0.8f, 0.8f},
                    material(colours.at(i), 0.0f, 0.5f));
            }

            Fenrir::Entity light = entities.CreateEntity();
            // From the skybox's sun, ahead of the starting camera, up and to its right (make_skybox.py's SUN): the
            // light shines along its entity's -Z, away from the sun.
            light.AddComponent<Fenrir::Transform>().rot =
                glm::quatLookAt(glm::normalize(Vec3(-0.35f, -0.35f, 1.0f)), Vec3(0.0f, 1.0f, 0.0f));
            light.AddComponent<Fenrir::DirectionalLight>();
            // The sky lights the plinth as well as filling the background (ADR-0047), except Darcy Wilson's.
            ApplySky(scene, state);
            scene.AddResource<SkyState>(state);
        }

        void Update(Fenrir::App& app, Fenrir::Scene& scene)
        {
            SkyState* state = scene.GetResource<SkyState>();
            if (state == nullptr)
            {
                return;
            }
            state->seconds += static_cast<float>(app.GetTime().deltaTime);
            if (state->spinner.IsValid())
            {
                state->spinner.GetComponent<Fenrir::Transform>().rot =
                    Fenrir::Math::AngleAxis(state->seconds * 0.7f, Vec3(0.0f, 1.0f, 0.0f));
            }

            const auto& keyboard = app.GetResource<Fenrir::Keyboard>();
            if (keyboard.JustPressed(Fenrir::Key::B))
            {
                state->cubemapSky = !state->cubemapSky;
                ApplySky(scene, *state);
                app.Logger()->Info("B: the scene's Sky is {}",
                                   state->cubemapSky ? "the skybox cubemap" : "one dusk colour");
            }
            if (keyboard.JustPressed(Fenrir::Key::C))
            {
                state->darcySky = !state->darcySky;
                ApplySky(scene, *state);
                app.Logger()->Info("C: the skybox cubemap is {}{}",
                                   state->darcySky ? "Darcy Wilson's, lit by a neutral grey SkyLight"
                                                   : "the generated one, which lights the scene itself",
                                   state->cubemapSky ? "" : " (shown once B brings the cubemap back)");
            }
            if (keyboard.JustPressed(Fenrir::Key::O) && state->camera.IsValid())
            {
                state->overridden = !state->overridden;
                Fenrir::Camera& camera = state->camera.GetComponent<Fenrir::Camera>();
                if (state->overridden)
                {
                    camera.background = Fenrir::ColourBackground{OverrideColour};
                }
                else
                {
                    camera.background = Fenrir::SceneBackground{};
                }
                app.Logger()->Info("O: the camera's background is {}",
                                   state->overridden ? "its own teal colour" : "the scene's Sky");
            }
            if (keyboard.JustPressed(Fenrir::Key::T) && state->camera.IsValid())
            {
                state->tonemapper = (state->tonemapper + 1) % Tonemappers.size();
                state->camera.GetComponent<Fenrir::Camera>().tonemapper = Tonemappers.at(state->tonemapper).tonemapper;
                app.Logger()->Info("T: the camera's tonemapper is {}", Tonemappers.at(state->tonemapper).name);
            }
            if (keyboard.JustPressed(Fenrir::Key::E) && state->camera.IsValid())
            {
                state->exposure = (state->exposure + 1) % Exposures.size();
                state->camera.GetComponent<Fenrir::Camera>().exposure = Exposures.at(state->exposure);
                app.Logger()->Info("E: the camera's exposure is {:+.0f} EV", Exposures.at(state->exposure));
            }
        }
    } // namespace

    FenrirDemo::DemoScene Sky()
    {
        FenrirDemo::DemoScene scene;
        scene.name = "Sky";
        scene.keyHelp =
            "B: the scene's Sky, the skybox cubemap or a dusk colour. C: the skybox, generated or Darcy Wilson's (lit "
            "by a neutral grey SkyLight). O: "
            "the camera's own background, a teal colour overriding the Sky, on or off. T: the camera's tonemapper "
            "(AgX, PBR Neutral, ACES, None). E: the camera's exposure (0, +1, +2, -1 EV).";
        scene.keys = {Fenrir::Key::B, Fenrir::Key::O, Fenrir::Key::B, Fenrir::Key::O, Fenrir::Key::C,
                      Fenrir::Key::C, Fenrir::Key::T, Fenrir::Key::E, Fenrir::Key::T, Fenrir::Key::E,
                      Fenrir::Key::T, Fenrir::Key::E, Fenrir::Key::T, Fenrir::Key::E};
        scene.camera.position = {0.0f, 1.5f, 7.0f};
        scene.spawn = Spawn;
        scene.update = Update;
        return scene;
    }
} // namespace RendererDemo
