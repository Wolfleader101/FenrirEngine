#include <array>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirRenderer/SpawnModel.hpp"
#include "FenrirScene/Scene.hpp"
#include "FetchedAssets.hpp"
#include "LightingPanel.hpp"
#include "LightingScenes.hpp"

namespace LightingDemo
{
    namespace
    {
        using Fenrir::Math::Vec3;

        constexpr const char* SceneName = "Showcase models";

        /** @brief A fetched model, and where its root stands: each is scaled to about two metres. */
        struct Showcase
        {
            const char* path;
            Vec3 position;
            float scale;
        };
        // DamagedHelmet is about 2 across, FlightHelmet 0.72 tall standing on its origin, SciFiHelmet about 2.9 tall.
        const std::array<Showcase, 3> Models{{{"DamagedHelmet/DamagedHelmet.gltf", {-3.0f, 1.5f, 0.0f}, 1.0f},
                                              {"FlightHelmet/FlightHelmet.gltf", {0.0f, 0.5f, 0.0f}, 2.8f},
                                              {"SciFiHelmet/SciFiHelmet.gltf", {3.0f, 1.5f, 0.0f}, 0.7f}}};

        struct FetchedPanorama
        {
            const char* path;
            const char* name;
        };
        constexpr std::array<FetchedPanorama, 3> Panoramas{
            {{"panoramas/venice_sunset_2k.hdr", "Venice Sunset (2k)"},
             {"panoramas/studio_small_09_2k.hdr", "Studio Small 09 (2k)"},
             {"panoramas/symmetrical_garden_02_2k.hdr", "Symmetrical Garden 02 (2k)"}}};

        /** @brief The helmets and what the panel and keys have set, kept as a scene resource. */
        struct ShowcaseState
        {
            LightingSettings lighting;
            /** @brief Each model's root; empty when the fetched assets are missing. */
            std::vector<Fenrir::Entity> models;
            bool missingAssets = false;
            bool turntable = false;
            float seconds = 0.0f;
        };

        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            ShowcaseState state;
            std::vector<std::string> models;
            std::vector<std::string> panoramas;
            for (const Showcase& model : Models)
            {
                if (const std::optional<std::string> path = FetchedAsset(model.path))
                {
                    models.push_back(*path);
                }
            }
            for (const FetchedPanorama& panorama : Panoramas)
            {
                if (const std::optional<std::string> path = FetchedAsset(panorama.path))
                {
                    panoramas.push_back(*path);
                }
            }
            // All or nothing: configure fetches every file or fails.
            if (models.size() != Models.size() || panoramas.size() != Panoramas.size())
            {
                state.missingAssets = true;
                app.Logger()->Info("{}: the fetched demo assets are missing, so the scene is empty "
                                   "(FENRIR_FETCH_DEMO_ASSETS, ADR-0052)",
                                   SceneName);
                scene.AddResource<ShowcaseState>(state);
                return;
            }

            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            for (std::size_t i = 0; i < Panoramas.size(); ++i)
            {
                const auto image = store.Load<Fenrir::Image>(panoramas.at(i), scene);
                if (!image.Ok())
                {
                    app.Logger()->Error("{}: the panorama {} did not load from the cache", SceneName,
                                        Panoramas.at(i).path);
                    continue;
                }
                state.lighting.panoramas.push_back({Panoramas.at(i).name, image.handle});
            }
            for (std::size_t i = 0; i < Models.size(); ++i)
            {
                const auto model = store.Load<Fenrir::Model>(models.at(i), scene);
                if (!model.Ok())
                {
                    app.Logger()->Error("{}: {} did not load from the cache", SceneName, Models.at(i).path);
                    continue;
                }
                Fenrir::Entity root = Fenrir::SpawnModel(app, scene, model.handle);
                if (root.IsValid())
                {
                    auto& transform = root.GetComponent<Fenrir::Transform>();
                    transform.pos = Models.at(i).position;
                    transform.scale = Vec3(Models.at(i).scale);
                    state.models.push_back(root);
                }
            }
            scene.AddResource<ShowcaseState>(state);
        }

        void Update(Fenrir::App& app, Fenrir::Scene& scene)
        {
            ShowcaseState* state = scene.GetResource<ShowcaseState>();
            if (state == nullptr)
            {
                return;
            }
            if (state->missingAssets)
            {
                ShowMissingAssets(scene, SceneName);
            }
            if (app.GetResource<Fenrir::Keyboard>().JustPressed(Fenrir::Key::R))
            {
                state->turntable = !state->turntable;
                app.Logger()->Info("R: the turntable is {}", state->turntable ? "on" : "off");
            }
            RunLightingPanel(app, scene, state->lighting, [&] { ImGui::Checkbox("Turntable (R)", &state->turntable); });

            if (state->turntable)
            {
                state->seconds += static_cast<float>(app.GetTime().deltaTime);
            }
            for (Fenrir::Entity& model : state->models)
            {
                if (model.IsValid())
                {
                    model.GetComponent<Fenrir::Transform>().rot =
                        Fenrir::Math::AngleAxis(state->seconds * 0.5f, Vec3(0.0f, 1.0f, 0.0f));
                }
            }
        }
    } // namespace

    FenrirDemo::DemoScene ShowcaseModels()
    {
        FenrirDemo::DemoScene scene;
        scene.name = SceneName;
        scene.keyHelp = LightingKeyHelp(true) + " R: the turntable on or off.";
        scene.keys = {Fenrir::Key::S, Fenrir::Key::T, Fenrir::Key::B, Fenrir::Key::R,
                      Fenrir::Key::S, Fenrir::Key::E, Fenrir::Key::B, Fenrir::Key::R};
        scene.camera.position = {0.0f, 1.5f, 7.0f};
        scene.spawn = Spawn;
        scene.update = Update;
        return scene;
    }
} // namespace LightingDemo
