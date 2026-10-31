#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

#include <imgui.h>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
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

        constexpr float Pi = std::numbers::pi_v<float>;
        /** @brief The most lights the scene holds; the panel shows any number up to it. */
        constexpr int MaxLights = 256;
        /** @brief The counts M steps through. */
        constexpr std::array<int, 4> CountSteps{16, 64, 100, 256};
        /** @brief Half the floor's side, in metres. */
        constexpr float HalfRoom = 10.0f;
        /** @brief Every fourth light is a spot light. */
        constexpr int SpotEvery = 4;
        constexpr float MarkerSize = 0.12f;

        /** @brief One moving light: its entity, its marker sphere, and the path it follows. */
        struct MovingLight
        {
            Fenrir::Entity entity;
            Fenrir::Entity marker;
            bool spot = false;
            Fenrir::Colour colour;
            /** @brief Its orbit about the room's centre: radius, height, angular speed and phase. */
            float radius = 0.0f;
            float height = 0.0f;
            float speed = 0.0f;
            float phase = 0.0f;
        };

        /** @brief The room's lights and what the panel and keys have set, kept as a scene resource. */
        struct ManyLightsState
        {
            LightingSettings lighting;
            std::vector<MovingLight> lights;
            int count = 100;
            bool moving = true;
            float time = 0.0f;
            float pointIntensity = 2.0f;
            float spotIntensity = 8.0f;
            float range = 5.0f;
        };

        /** @brief Hue `hue` (0 to 1 round the wheel) at full saturation and value, linear. */
        Fenrir::Colour HueColour(float hue)
        {
            const auto channel = [&](float offset) {
                const float k = std::fmod(hue * 6.0f + offset, 6.0f);
                return 1.0f - std::max(0.0f, std::min({k, 4.0f - k, 1.0f}));
            };
            return {channel(5.0f), channel(3.0f), channel(1.0f), 1.0f};
        }

        /** @brief A builtin:lit material of `colour`, `metallic` and `roughness`. */
        Fenrir::AssetHandle<Fenrir::Material> LitMaterial(Fenrir::AssetStore& store,
                                                          Fenrir::AssetHandle<Fenrir::Shader> lit, Fenrir::Scene& scene,
                                                          Fenrir::Colour colour, float metallic, float roughness)
        {
            const auto material = store.CreateMaterial(lit, scene);
            store.SetColour(material, "baseColour", colour);
            store.SetFloat(material, "metallic", metallic);
            store.SetFloat(material, "roughness", roughness);
            return material;
        }

        Fenrir::Entity Box(Fenrir::EntityList& entities, Fenrir::AssetHandle<Fenrir::Mesh> cube,
                           Fenrir::AssetHandle<Fenrir::Material> material, Vec3 position, Vec3 scale)
        {
            Fenrir::Entity entity = entities.CreateEntity();
            Fenrir::Transform& transform = entity.AddComponent<Fenrir::Transform>();
            transform.pos = position;
            transform.scale = scale;
            entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{cube, material});
            return entity;
        }

        /** @brief The floor in tiles of varying roughness and metal, four walls, and columns and spheres to light. */
        void SpawnRoom(Fenrir::AssetStore& store, Fenrir::Scene& scene, Fenrir::AssetHandle<Fenrir::Shader> lit,
                       Fenrir::AssetHandle<Fenrir::Mesh> cube, Fenrir::AssetHandle<Fenrir::Mesh> sphere)
        {
            Fenrir::EntityList& entities = scene.GetEntityList();
            // Five by five tiles, rough to smooth across and dielectric to metal back to front, all a light grey.
            constexpr int Tiles = 5;
            constexpr float Tile = 2.0f * HalfRoom / Tiles;
            for (int z = 0; z < Tiles; ++z)
            {
                for (int x = 0; x < Tiles; ++x)
                {
                    const float roughness = 0.1f + 0.8f * static_cast<float>(x) / (Tiles - 1);
                    const float metallic = static_cast<float>(z) / (Tiles - 1);
                    const auto material = LitMaterial(store, lit, scene, {0.6f, 0.6f, 0.6f, 1.0f}, metallic, roughness);
                    Box(entities, cube, material,
                        {-HalfRoom + Tile * (static_cast<float>(x) + 0.5f), -0.1f,
                         -HalfRoom + Tile * (static_cast<float>(z) + 0.5f)},
                        {Tile, 0.2f, Tile});
                }
            }

            const auto wall = LitMaterial(store, lit, scene, {0.8f, 0.78f, 0.75f, 1.0f}, 0.0f, 0.8f);
            constexpr float WallHeight = 4.0f;
            Box(entities, cube, wall, {0.0f, WallHeight * 0.5f, -HalfRoom}, {2.0f * HalfRoom, WallHeight, 0.2f});
            Box(entities, cube, wall, {0.0f, WallHeight * 0.5f, HalfRoom}, {2.0f * HalfRoom, WallHeight, 0.2f});
            Box(entities, cube, wall, {-HalfRoom, WallHeight * 0.5f, 0.0f}, {0.2f, WallHeight, 2.0f * HalfRoom});
            Box(entities, cube, wall, {HalfRoom, WallHeight * 0.5f, 0.0f}, {0.2f, WallHeight, 2.0f * HalfRoom});

            // A ring of columns, alternately polished metal and rough plastic, and spheres between them.
            const auto metal = LitMaterial(store, lit, scene, {0.95f, 0.93f, 0.88f, 1.0f}, 1.0f, 0.25f);
            const auto plastic = LitMaterial(store, lit, scene, {0.2f, 0.25f, 0.7f, 1.0f}, 0.0f, 0.6f);
            const auto gold = LitMaterial(store, lit, scene, {1.0f, 0.77f, 0.34f, 1.0f}, 1.0f, 0.4f);
            const auto white = LitMaterial(store, lit, scene, {0.9f, 0.9f, 0.9f, 1.0f}, 0.0f, 0.15f);
            constexpr int Columns = 8;
            for (int i = 0; i < Columns; ++i)
            {
                const float angle = 2.0f * Pi * static_cast<float>(i) / Columns;
                const Vec3 at{std::cos(angle) * 5.0f, 1.5f, std::sin(angle) * 5.0f};
                Box(entities, cube, i % 2 == 0 ? metal : plastic, at, {0.6f, 3.0f, 0.6f});
                const float between = angle + Pi / Columns;
                Fenrir::Entity ball = entities.CreateEntity();
                Fenrir::Transform& transform = ball.AddComponent<Fenrir::Transform>();
                transform.pos = {std::cos(between) * 5.0f, 0.6f, std::sin(between) * 5.0f};
                transform.scale = Vec3(1.2f);
                ball.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{sphere, i % 2 == 0 ? gold : white});
            }
        }

        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            ManyLightsState state;
            // A dark room: the lights are what shows.
            scene.AddResource<Fenrir::Sky>(Fenrir::ColourSky{{0.002f, 0.002f, 0.003f, 1.0f}});

            const auto cube = store.Load<Fenrir::Mesh>("cube.obj#Mesh0", scene);
            const auto sphere = store.Load<Fenrir::Mesh>("sphere.obj#Mesh0", scene);
            if (!cube.Ok() || !sphere.Ok())
            {
                app.Logger()->Error("Many lights: the cube or the sphere did not load from the asset root");
                scene.AddResource<ManyLightsState>(state);
                return;
            }
            const auto lit = store.Load<Fenrir::Shader>("builtin:lit", scene).handle;
            const auto unlit = store.Load<Fenrir::Shader>("builtin:unlit", scene).handle;
            SpawnRoom(store, scene, lit, cube.handle, sphere.handle);

            // Every light is made now and the ones past the count switched off, so the slider spawns nothing.
            Fenrir::EntityList& entities = scene.GetEntityList();
            for (int i = 0; i < MaxLights; ++i)
            {
                MovingLight light;
                light.spot = i % SpotEvery == SpotEvery - 1;
                // The golden ratio spreads neighbours' hues and paths apart.
                const float spread = std::fmod(static_cast<float>(i) * 0.618034f, 1.0f);
                light.colour = HueColour(spread);
                light.radius = 1.5f + 7.5f * std::fmod(static_cast<float>(i) * 0.381966f, 1.0f);
                light.height = light.spot ? 3.5f : 0.4f + 1.6f * spread;
                light.speed = (0.15f + 0.35f * spread) * (i % 2 == 0 ? 1.0f : -1.0f);
                light.phase = 2.0f * Pi * spread;

                light.entity = entities.CreateEntity();
                light.entity.AddComponent<Fenrir::Transform>();
                if (light.spot)
                {
                    light.entity.AddComponent<Fenrir::SpotLight>(
                        Fenrir::SpotLight{light.colour, state.spotIntensity, state.range + 2.0f, 0.25f, 0.5f, false});
                }
                else
                {
                    light.entity.AddComponent<Fenrir::PointLight>(
                        Fenrir::PointLight{light.colour, state.pointIntensity, state.range, false});
                }

                // A small sphere of the light's colour, bright enough to bloom.
                const auto glow = store.CreateMaterial(unlit, scene);
                store.SetColour(glow, "baseColour",
                                {light.colour.r * 4.0f, light.colour.g * 4.0f, light.colour.b * 4.0f, 1.0f});
                light.marker = entities.CreateEntity();
                light.marker.AddComponent<Fenrir::Transform>().scale = Vec3(MarkerSize);
                light.marker.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{sphere.handle, glow});
                state.lights.push_back(light);
            }
            scene.AddResource<ManyLightsState>(state);
        }

        /** @brief Where light `light` is at `time`, and for a spot, the way it points. */
        void Place(const MovingLight& light, float time, Fenrir::Transform& transform)
        {
            const float angle = light.phase + light.speed * time;
            const float bob = light.spot ? 0.0f : 0.3f * std::sin(time * 1.3f + light.phase * 3.0f);
            transform.pos = {std::cos(angle) * light.radius, light.height + bob, std::sin(angle) * light.radius};
            if (light.spot)
            {
                // Down, leaning out from the room's centre and swinging round as it goes.
                const Vec3 lean{std::cos(angle * 2.0f) * 0.5f, -1.0f, std::sin(angle * 2.0f) * 0.5f};
                transform.rot = glm::quatLookAt(glm::normalize(lean), Vec3(1.0f, 0.0f, 0.0f));
            }
        }

        void Update(Fenrir::App& app, Fenrir::Scene& scene)
        {
            ManyLightsState* state = scene.GetResource<ManyLightsState>();
            if (state == nullptr)
            {
                return;
            }
            const Fenrir::Keyboard& keyboard = app.GetResource<Fenrir::Keyboard>();
            if (keyboard.JustPressed(Fenrir::Key::M))
            {
                const auto next =
                    std::find_if(CountSteps.begin(), CountSteps.end(), [&](int step) { return step > state->count; });
                state->count = next == CountSteps.end() ? CountSteps.front() : *next;
                app.Logger()->Info("M: {} lights", state->count);
            }
            if (keyboard.JustPressed(Fenrir::Key::P))
            {
                state->moving = !state->moving;
                app.Logger()->Info("P: the lights are {}", state->moving ? "moving" : "still");
            }

            RunLightingPanel(app, scene, state->lighting, [&] {
                ImGui::SliderInt("Lights", &state->count, 0, MaxLights);
                ImGui::Checkbox("Moving (P)", &state->moving);
                ImGui::SliderFloat("Point intensity", &state->pointIntensity, 0.0f, 10.0f);
                ImGui::SliderFloat("Spot intensity", &state->spotIntensity, 0.0f, 30.0f);
                ImGui::SliderFloat("Range", &state->range, 0.5f, 15.0f);
            });

            if (state->moving)
            {
                state->time += static_cast<float>(app.GetTime().deltaTime);
            }
            for (int i = 0; i < static_cast<int>(state->lights.size()); ++i)
            {
                MovingLight& light = state->lights[static_cast<std::size_t>(i)];
                if (!light.entity.IsValid() || !light.marker.IsValid())
                {
                    continue;
                }
                const bool on = i < state->count;
                Fenrir::Transform& transform = light.entity.GetComponent<Fenrir::Transform>();
                Place(light, state->time, transform);
                light.marker.GetComponent<Fenrir::Transform>().pos = transform.pos;
                light.marker.GetComponent<Fenrir::MeshRenderer>().visible = on;
                if (light.spot)
                {
                    Fenrir::SpotLight& spot = light.entity.GetComponent<Fenrir::SpotLight>();
                    spot.intensity = on ? state->spotIntensity : 0.0f;
                    spot.range = state->range + 2.0f;
                }
                else
                {
                    Fenrir::PointLight& point = light.entity.GetComponent<Fenrir::PointLight>();
                    point.intensity = on ? state->pointIntensity : 0.0f;
                    point.range = state->range;
                }
            }
        }
    } // namespace

    FenrirDemo::DemoScene ManyLights()
    {
        FenrirDemo::DemoScene scene;
        scene.name = "Many lights";
        scene.keyHelp = LightingKeyHelp(false) + " M: 16, 64, 100 or 256 lights. P: the lights moving or still.";
        scene.keys = {Fenrir::Key::T, Fenrir::Key::E, Fenrir::Key::B, Fenrir::Key::M,
                      Fenrir::Key::P, Fenrir::Key::M, Fenrir::Key::P, Fenrir::Key::M};
        // Inside the room, by its front wall, looking down across it.
        scene.camera.position = {0.0f, 4.5f, 9.0f};
        scene.camera.pitch = -0.45f;
        scene.spawn = Spawn;
        scene.update = Update;
        return scene;
    }
} // namespace LightingDemo
