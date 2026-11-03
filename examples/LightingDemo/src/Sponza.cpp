#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "DemoMaterials.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirRenderer/Renderer.hpp"
#include "FenrirRenderer/SpawnModel.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirWindow/Window.hpp"
#include "FetchedAssets.hpp"
#include "LightingPanel.hpp"
#include "LightingScenes.hpp"

namespace LightingDemo
{
    namespace
    {
        using Fenrir::Math::Vec3;

        constexpr const char* SceneName = "Sponza";
        const Vec3 Up{0.0f, 1.0f, 0.0f};

        constexpr const char* Model = "Sponza/Sponza.gltf";
        constexpr const char* Panorama = "panoramas/kloofendal_48d_partly_cloudy_puresky_2k.hdr";

        /**
         * @brief The way to the sun in the panorama unturned: Kloofendal 48d's sun (its brightest texels' centroid),
         * 48 degrees up. The sun is turned with the sky each frame, so the sky's rotation moves both.
         */
        const Vec3 PanoramaToSun{0.3775f, 0.7418f, -0.5543f};
        /**
         * @brief The sky's rotation at first, radians about +Y: the sun high over the courtyard's east (+X) end and a
         * little to the south, so that it shines down the courtyard's length onto the west end's walls and floor and
         * the north arcade's faces.
         */
        constexpr float SkyRotation = -0.834f;
        const Fenrir::Colour SunColour{1.0f, 0.93f, 0.82f, 1.0f};

        // The model's ground-floor colonnades run along X either side of the courtyard: their middle columns stand
        // at these X, centred on these Z, about 0.28 in radius at a torch's height.
        constexpr std::array<float, 2> ColumnX{-1.96f, 0.96f};
        constexpr std::array<float, 2> ColumnZ{-2.2f, 1.66f};
        constexpr float ColumnRadius = 0.28f;
        /** @brief The flames' height over the floor, and how far each stands out from its column's surface. */
        constexpr float FlameHeight = 1.55f;
        constexpr float FlameStandoff = 0.3f;
        const Fenrir::Colour TorchColour{1.0f, 0.52f, 0.2f, 1.0f};

        /** @brief A torch: its point light, and the phase its flicker starts at. */
        struct Torch
        {
            Fenrir::Entity light;
            float phase = 0.0f;
        };

        /** @brief The scene and what the panel and keys have set, kept as a scene resource. */
        struct SponzaState
        {
            LightingSettings lighting;
            Fenrir::Entity sun;
            std::vector<Torch> torches;
            bool fetched = false;
            float sunIntensity = 6.0f;
            float torchIntensity = 6.0f;
            float torchRange = 9.0f;
            bool torchesLit = true;
            bool torchShadows = true;
            bool flicker = true;
            /** @brief Whether presents wait for the display; off, the frame time is the frame's own. */
            bool vsync = true;
            bool appliedVsync = true;
            /** @brief How long the torches have flickered, in seconds. */
            float flickerSeconds = 0.0f;
            /** @brief The frame time, smoothed over about a second, in seconds. */
            float frameTime = 0.0f;
        };

        /** @brief `direction` in the panorama, turned about +Y by a sky's `rotation` into the world (Frame.glsl's
         * FenrirTurnedBack, inverted). */
        Vec3 TurnedBySky(Vec3 direction, float rotation)
        {
            const float c = std::cos(rotation);
            const float s = std::sin(rotation);
            return {c * direction.x + s * direction.z, direction.y, -s * direction.x + c * direction.z};
        }

        /** @brief A box of `size` centred at `position`. */
        void AddBox(Fenrir::Scene& scene, Fenrir::AssetHandle<Fenrir::Mesh> cube,
                    Fenrir::AssetHandle<Fenrir::Material> material, Vec3 position, Vec3 size, bool castShadows = true)
        {
            Fenrir::Entity box = scene.GetEntityList().CreateEntity();
            auto& transform = box.AddComponent<Fenrir::Transform>();
            transform.pos = position;
            transform.scale = size;
            box.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{cube, material, true, castShadows});
        }

        /**
         * @brief Without the fetched model: a floor, the courtyard's two colonnades of plain columns and the galleries'
         * outer walls, in the model's places, so the sun and the torches still have something to light and shadow.
         */
        void SpawnStandIn(Fenrir::AssetStore& store, Fenrir::Scene& scene, Fenrir::AssetHandle<Fenrir::Mesh> cube)
        {
            const auto stone = LitMaterial(store, scene, {0.5f, 0.45f, 0.38f, 1.0f}, 0.0f, 0.8f);
            AddBox(scene, cube, stone, {-0.5f, -0.1f, -0.25f}, {22.0f, 0.2f, 10.0f});
            for (const float z : ColumnZ)
            {
                for (float x = -7.9f; x < 8.0f; x += 2.95f)
                {
                    AddBox(scene, cube, stone, {x, 2.0f, z}, {2.0f * ColumnRadius, 4.0f, 2.0f * ColumnRadius});
                }
            }
            for (const float z : {-5.15f, 4.6f})
            {
                AddBox(scene, cube, stone, {-0.5f, 3.0f, z}, {22.0f, 6.0f, 0.3f});
            }
        }

        /**
         * @brief A torch on the gallery side of each middle column: an iron bracket, a small flame that glows and
         * blooms, and a shadowed point light in it. Four, RendererConfig's default point shadow budget, so every
         * torch keeps its shadows and none lights through a column or a wall.
         */
        void SpawnTorches(Fenrir::AssetStore& store, Fenrir::Scene& scene, Fenrir::AssetHandle<Fenrir::Mesh> cube,
                          Fenrir::AssetHandle<Fenrir::Mesh> sphere, SponzaState& state)
        {
            const auto iron = LitMaterial(store, scene, {0.04f, 0.035f, 0.03f, 1.0f}, 1.0f, 0.55f);
            const auto flame = UnlitMaterial(store, scene, {12.0f, 5.0f, 1.6f, 1.0f});
            Fenrir::EntityList& entities = scene.GetEntityList();
            for (const float x : ColumnX)
            {
                for (const float z : ColumnZ)
                {
                    // Away from the courtyard, into the gallery behind the column.
                    const float out = z < 0.0f ? -1.0f : 1.0f;
                    const float surface = z + out * ColumnRadius;
                    const Vec3 flamePosition{x, FlameHeight, surface + out * FlameStandoff};
                    // The bracket and the flame cast no shadow: round the light, they would darken all it lights.
                    AddBox(scene, cube, iron, {x, FlameHeight - 0.16f, surface + out * FlameStandoff * 0.5f},
                           {0.05f, 0.05f, FlameStandoff}, false);
                    AddBox(scene, cube, iron, {x, FlameHeight - 0.12f, flamePosition.z}, {0.12f, 0.08f, 0.12f}, false);
                    Fenrir::Entity glow = entities.CreateEntity();
                    auto& transform = glow.AddComponent<Fenrir::Transform>();
                    transform.pos = flamePosition;
                    transform.scale = {0.08f, 0.16f, 0.08f};
                    glow.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{sphere, flame, true, false});

                    Torch torch;
                    torch.light = entities.CreateEntity();
                    torch.light.AddComponent<Fenrir::Transform>().pos = flamePosition + Up * 0.12f;
                    torch.light.AddComponent<Fenrir::PointLight>(
                        Fenrir::PointLight{TorchColour, state.torchIntensity, state.torchRange, state.torchShadows});
                    torch.phase = static_cast<float>(state.torches.size()) * 1.7f;
                    state.torches.push_back(torch);
                }
            }
        }

        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            SponzaState state;
            state.lighting.skyRotation = SkyRotation;
            // The sky lights the galleries' shade with no probe to stop it, so it is drawn dim beside the sun; the
            // torches carry the galleries.
            state.lighting.skyIntensity = 0.35f;
            state.lighting.ambientOcclusion.radius = 1.0f;
            // The building is 30 metres long: the cascades cover it from anywhere inside.
            state.lighting.sunShadows.shadowDistance = 40.0f;

            const std::optional<std::string> model = FetchedAsset(Model);
            const std::optional<std::string> panorama = FetchedAsset(Panorama);
            if (panorama)
            {
                if (const auto image = store.Load<Fenrir::Image>(*panorama, scene); image.Ok())
                {
                    state.lighting.panoramas.push_back({"Kloofendal 48d Partly Cloudy (2k)", image.handle});
                }
                else
                {
                    app.Logger()->Error("{}: the panorama {} did not load from the cache", SceneName, Panorama);
                }
            }
            if (state.lighting.panoramas.empty())
            {
                scene.AddResource<Fenrir::Sky>(Fenrir::ColourSky{{0.35f, 0.5f, 0.75f, 1.0f}});
            }
            // Every torch shadowed, clustered and with vsync, whatever another scene left them as.
            Fenrir::Renderer::SetPointShadowBudget(app, 4);
            Fenrir::Renderer::SetClusteredLights(app, true);
            Fenrir::Window::SetSwapInterval(app, Fenrir::Window::MainWindow(), 1);

            const auto cube = store.Load<Fenrir::Mesh>("cube.obj#Mesh0", scene);
            const auto sphere = store.Load<Fenrir::Mesh>("sphere.obj#Mesh0", scene);
            if (!cube.Ok() || !sphere.Ok())
            {
                app.Logger()->Error("{}: the cube or the sphere did not load from the asset root", SceneName);
                scene.AddResource<SponzaState>(std::move(state));
                return;
            }
            bool spawned = false;
            if (model)
            {
                if (const auto loaded = store.Load<Fenrir::Model>(*model, scene); loaded.Ok())
                {
                    spawned = Fenrir::SpawnModel(app, scene, loaded.handle).IsValid();
                }
                if (!spawned)
                {
                    app.Logger()->Error("{}: {} did not load from the cache", SceneName, Model);
                }
            }
            if (!spawned)
            {
                SpawnStandIn(store, scene, cube.handle);
            }
            // Shown on screen whenever either is missing, in the cache or loading from it.
            state.fetched = spawned && !state.lighting.panoramas.empty();
            if (!model || !panorama)
            {
                app.Logger()->Info("{}: the model and the sky are fetched assets (FENRIR_FETCH_DEMO_ASSETS); "
                                   "plain colonnades and a blue sky stand in",
                                   SceneName);
            }
            SpawnTorches(store, scene, cube.handle, sphere.handle, state);

            state.sun = scene.GetEntityList().CreateEntity();
            state.sun.AddComponent<Fenrir::Transform>();
            state.sun.AddComponent<Fenrir::DirectionalLight>(Fenrir::DirectionalLight{SunColour, state.sunIntensity});
            scene.AddResource<SponzaState>(std::move(state));
        }

        void Update(Fenrir::App& app, Fenrir::Scene& scene)
        {
            SponzaState* state = scene.GetResource<SponzaState>();
            if (state == nullptr)
            {
                return;
            }
            if (!state->fetched)
            {
                ShowMissingAssets(scene, SceneName);
            }
            const Fenrir::Keyboard& keyboard = app.GetResource<Fenrir::Keyboard>();
            if (keyboard.JustPressed(Fenrir::Key::J))
            {
                state->torchShadows = !state->torchShadows;
                app.Logger()->Info("J: the torches' shadows are {}", state->torchShadows ? "on" : "off");
            }
            if (keyboard.JustPressed(Fenrir::Key::K))
            {
                state->torchesLit = !state->torchesLit;
                app.Logger()->Info("K: the torches are {}", state->torchesLit ? "lit" : "out");
            }
            if (keyboard.JustPressed(Fenrir::Key::N))
            {
                state->flicker = !state->flicker;
                app.Logger()->Info("N: the torches {}", state->flicker ? "flicker" : "burn steadily");
            }

            const auto delta = static_cast<float>(app.GetTime().deltaTime);
            state->frameTime = state->frameTime == 0.0f ? delta : glm::mix(state->frameTime, delta, 0.05f);
            const Fenrir::RendererStats stats = Fenrir::Renderer::Stats(app);
            RunLightingPanel(app, scene, state->lighting, [&] {
                ImGui::Text("Frame: %.2f ms (%.0f fps)", state->frameTime * 1000.0f,
                            state->frameTime > 0.0f ? 1.0f / state->frameTime : 0.0f);
                ImGui::Text("Draws: %u  Instances: %u  Shadow draws: %u", stats.draws, stats.instances,
                            stats.shadowDraws);
                ImGui::Checkbox("Vsync", &state->vsync);
                ImGui::SliderFloat("Sun intensity", &state->sunIntensity, 0.0f, 20.0f);
                ImGui::Checkbox("Torches lit (K)", &state->torchesLit);
                ImGui::Checkbox("Torch shadows (J)", &state->torchShadows);
                ImGui::Checkbox("Flicker (N)", &state->flicker);
                ImGui::SliderFloat("Torch intensity", &state->torchIntensity, 0.0f, 30.0f);
                ImGui::SliderFloat("Torch range", &state->torchRange, 1.0f, 20.0f);
            });

            if (state->vsync != state->appliedVsync)
            {
                // The main window's swap interval: the renderer presents to it. False on the null backend's no window.
                Fenrir::Window::SetSwapInterval(app, Fenrir::Window::MainWindow(), state->vsync ? 1 : 0);
                state->appliedVsync = state->vsync;
            }

            // The sun follows the sky as the panel turns it.
            if (state->sun.IsValid())
            {
                const Vec3 travel = -TurnedBySky(PanoramaToSun, state->lighting.skyRotation);
                state->sun.GetComponent<Fenrir::Transform>().rot = glm::quatLookAt(glm::normalize(travel), Up);
                state->sun.GetComponent<Fenrir::DirectionalLight>().intensity = state->sunIntensity;
            }
            if (state->flicker)
            {
                state->flickerSeconds += delta;
            }
            for (const Torch& torch : state->torches)
            {
                // Two fast waves out of step, a few percent each: a flame's unsteady light, not a strobe.
                const float t = state->flickerSeconds + torch.phase;
                const float flicker =
                    state->flicker ? 1.0f + 0.07f * std::sin(t * 11.0f) + 0.05f * std::sin(t * 23.0f + 1.3f) : 1.0f;
                auto& light = torch.light.GetComponent<Fenrir::PointLight>();
                light.intensity = state->torchesLit ? state->torchIntensity * flicker : 0.0f;
                light.range = state->torchRange;
                light.castShadows = state->torchShadows;
            }
        }
    } // namespace

    FenrirDemo::DemoScene Sponza()
    {
        FenrirDemo::DemoScene scene;
        scene.name = SceneName;
        scene.keyHelp = LightingKeyHelp(false) +
                        " J: the torches' shadows off or on. K: the torches out or lit. N: the flicker off or on.";
        // Through the debug views and back, the sun's and the torches' shadows off and on, the torches out and lit,
        // the flicker off and on, then the tonemapper, exposure, bloom and occlusion.
        scene.keys = {Fenrir::Key::G, Fenrir::Key::G, Fenrir::Key::G, Fenrir::Key::G, Fenrir::Key::G, Fenrir::Key::G,
                      Fenrir::Key::H, Fenrir::Key::H, Fenrir::Key::J, Fenrir::Key::J, Fenrir::Key::K, Fenrir::Key::K,
                      Fenrir::Key::N, Fenrir::Key::N, Fenrir::Key::T, Fenrir::Key::E, Fenrir::Key::B, Fenrir::Key::O};
        // On the courtyard floor at its east end, the sun behind, looking west along it to the sunlit end.
        scene.camera.position = {8.5f, 1.7f, -0.3f};
        scene.camera.yaw = 1.5708f;
        scene.camera.pitch = 0.1f;
        scene.spawn = Spawn;
        scene.update = Update;
        return scene;
    }
} // namespace LightingDemo
