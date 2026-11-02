#include <cstddef>
#include <format>
#include <vector>

#include <imgui.h>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirScene/Scene.hpp"
#include "LightingPanel.hpp"
#include "LightingScenes.hpp"

namespace LightingDemo
{
    namespace
    {
        using Fenrir::Math::Vec3;

        constexpr std::size_t Columns = 7;
        constexpr std::size_t Rows = 7;
        constexpr float Spacing = 1.2f;
        // LearnOpenGL's IBL grid's albedo, linear, for comparing with its result.
        constexpr Fenrir::Colour DefaultBaseColour{0.5f, 0.0f, 0.0f, 1.0f};
        constexpr float SunIntensity = 3.0f;

        /** @brief The grid's materials and sun, and what the panel and keys have set, kept as a scene resource. */
        struct GridState
        {
            LightingSettings lighting;
            /** @brief Row by row from the bottom, each row's from the left. */
            std::vector<Fenrir::AssetHandle<Fenrir::Material>> materials;
            Fenrir::Entity sun;
            bool sunOn = false;
            Fenrir::Colour baseColour = DefaultBaseColour;
        };

        float Fraction(std::size_t index, std::size_t count)
        {
            return static_cast<float>(index) / static_cast<float>(count - 1);
        }

        Vec3 SpherePosition(std::size_t column, std::size_t row)
        {
            return {(static_cast<float>(column) - 0.5f * static_cast<float>(Columns - 1)) * Spacing,
                    (static_cast<float>(row) - 0.5f * static_cast<float>(Rows - 1)) * Spacing, 0.0f};
        }

        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            GridState state;

            // Committed (ADR-0052), so the grid is lit the same with or without the fetched assets.
            const auto studio = store.Load<Fenrir::Image>("panoramas/brown_photostudio_02_1k.hdr", scene);
            if (!studio.Ok())
            {
                app.Logger()->Error("Material grid: the studio panorama did not load from the asset root");
            }
            else
            {
                state.lighting.panoramas.push_back({"Brown Photostudio 02 (1k)", studio.handle});
            }

            const auto sphere = store.Load<Fenrir::Mesh>("sphere.obj#Mesh0", scene);
            if (!sphere.Ok())
            {
                app.Logger()->Error("Material grid: the sphere did not load from the asset root");
                scene.AddResource<GridState>(state);
                return;
            }
            const auto lit = store.Load<Fenrir::Shader>("builtin:lit", scene).handle;
            Fenrir::EntityList& entities = scene.GetEntityList();
            for (std::size_t row = 0; row < Rows; ++row)
            {
                for (std::size_t column = 0; column < Columns; ++column)
                {
                    const auto material = store.CreateMaterial(lit, scene);
                    store.SetColour(material, "baseColour", state.baseColour);
                    store.SetFloat(material, "metallic", Fraction(row, Rows));
                    store.SetFloat(material, "roughness", Fraction(column, Columns));
                    state.materials.push_back(material);

                    Fenrir::Entity entity = entities.CreateEntity();
                    entity.AddComponent<Fenrir::Transform>().pos = SpherePosition(column, row);
                    entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{sphere.handle, material});
                }
            }

            // Off at first, so the grid shows the sky's light alone, as LearnOpenGL's IBL result does.
            state.sun = entities.CreateEntity();
            state.sun.AddComponent<Fenrir::Transform>().rot =
                glm::quatLookAt(glm::normalize(Vec3(-0.4f, -0.5f, -1.0f)), Vec3(0.0f, 1.0f, 0.0f));
            state.sun.AddComponent<Fenrir::DirectionalLight>().intensity = 0.0f;
            scene.AddResource<GridState>(state);
        }

        void DrawLabels(Fenrir::Scene& scene)
        {
            Fenrir::DebugDraw* draw = scene.GetResource<Fenrir::DebugDraw>();
            if (draw == nullptr)
            {
                return;
            }
            const float below = SpherePosition(0, 0).y - 0.9f;
            for (std::size_t column = 0; column < Columns; ++column)
            {
                draw->WorldText({SpherePosition(column, 0).x, below, 0.0f},
                                std::format("{:.2f}", Fraction(column, Columns)), Fenrir::Colour::White);
            }
            draw->WorldText({0.0f, below - 0.5f, 0.0f}, "roughness", Fenrir::Colour::White);
            // On the right, clear of the Lighting window.
            const float right = SpherePosition(Columns - 1, 0).x + 0.9f;
            for (std::size_t row = 0; row < Rows; ++row)
            {
                draw->WorldText({right, SpherePosition(0, row).y, 0.0f}, std::format("{:.2f}", Fraction(row, Rows)),
                                Fenrir::Colour::White);
            }
            draw->WorldText({right, SpherePosition(0, Rows - 1).y + 0.8f, 0.0f}, "metallic", Fenrir::Colour::White);
        }

        void Update(Fenrir::App& app, Fenrir::Scene& scene)
        {
            GridState* state = scene.GetResource<GridState>();
            if (state == nullptr)
            {
                return;
            }
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            if (app.GetResource<Fenrir::Keyboard>().JustPressed(Fenrir::Key::L))
            {
                state->sunOn = !state->sunOn;
                app.Logger()->Info("L: the sun is {}", state->sunOn ? "on" : "off");
            }

            bool recolour = false;
            RunLightingPanel(app, scene, state->lighting, [&] {
                ImGui::Checkbox("Sun (L)", &state->sunOn);
                recolour = ImGui::ColorEdit3("Base colour (linear)", &state->baseColour.r, ImGuiColorEditFlags_Float);
            });
            if (recolour)
            {
                for (const auto& material : state->materials)
                {
                    store.SetColour(material, "baseColour", state->baseColour);
                }
            }
            if (state->sun.IsValid())
            {
                state->sun.GetComponent<Fenrir::DirectionalLight>().intensity = state->sunOn ? SunIntensity : 0.0f;
            }
            DrawLabels(scene);
        }
    } // namespace

    FenrirDemo::DemoScene MaterialGrid()
    {
        FenrirDemo::DemoScene scene;
        scene.name = "Material grid";
        scene.keyHelp = LightingKeyHelp(false) + " L: the sun on or off.";
        scene.keys = {Fenrir::Key::T, Fenrir::Key::E, Fenrir::Key::B, Fenrir::Key::L,
                      Fenrir::Key::T, Fenrir::Key::E, Fenrir::Key::B, Fenrir::Key::L};
        scene.camera.position = {0.0f, 0.0f, 10.0f};
        scene.spawn = Spawn;
        scene.update = Update;
        return scene;
    }
} // namespace LightingDemo
