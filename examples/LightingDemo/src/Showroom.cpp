#include <array>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
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
        using Fenrir::Math::Quat;
        using Fenrir::Math::Vec3;

        constexpr const char* SceneName = "Showroom";
        constexpr float Pi = std::numbers::pi_v<float>;
        /** @brief Half the room's width and depth, and its height, in metres. */
        constexpr float HalfWidth = 6.0f;
        constexpr float HalfDepth = 5.0f;
        constexpr float RoomHeight = 4.0f;
        /**
         * @brief The floor's roughness at its left edge and its right; between them it rises with the square of the
         * distance across, so most of the floor in view is glossy: about 0.05 a quarter of the way, 0.14 at the
         * middle, 0.29 three quarters of the way.
         */
        constexpr float SmoothestFloor = 0.02f;
        constexpr float RoughestFloor = 0.5f;
        /** @brief The plinths' side and height. */
        constexpr float PlinthSide = 0.8f;
        constexpr float PlinthHeight = 1.0f;

        /** @brief A fetched helmet on a plinth, scaled to about 0.8 m, and the height its root stands at above it. */
        struct Exhibit
        {
            const char* path;
            float x;
            float scale;
            float lift;
        };
        // DamagedHelmet is about 2 across about its origin, FlightHelmet 0.72 tall standing on its origin, SciFiHelmet
        // about 2.9 tall about its origin.
        constexpr std::array<Exhibit, 3> Exhibits{{{"DamagedHelmet/DamagedHelmet.gltf", -2.5f, 0.4f, 0.4f},
                                                   {"FlightHelmet/FlightHelmet.gltf", 0.0f, 1.1f, 0.0f},
                                                   {"SciFiHelmet/SciFiHelmet.gltf", 2.5f, 0.28f, 0.42f}}};

        /** @brief One rect light, the thin glowing slab that shows it, and the panel's name for it. */
        struct ShowroomLight
        {
            const char* name;
            Fenrir::Entity light;
            Fenrir::Entity slab;
            Fenrir::AssetHandle<Fenrir::Material> glow;
        };

        /** @brief The room's lights and what the panel and keys have set, kept as a scene resource. */
        struct ShowroomState
        {
            LightingSettings lighting;
            std::vector<ShowroomLight> lights;
            bool missingAssets = false;
            /** @brief Which of the size steps Z picks: every light's width and height times 0.5, 1 or 2. */
            int sizeStep = 1;
        };

        /** @brief The sizes Z steps through, as multiples of each light's own. */
        constexpr std::array<float, 3> SizeSteps{0.5f, 1.0f, 2.0f};

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

        /**
         * @brief A linear metallic-roughness texture whose green, the roughness, rises smoothly from SmoothestFloor at
         * its left edge to RoughestFloor at its right, with the square of the distance across, and whose blue, the
         * metal, is 0.
         */
        Fenrir::ImageData RoughnessRamp()
        {
            constexpr std::uint32_t Width = 256;
            constexpr std::uint32_t Height = 4;
            Fenrir::ImageData image;
            image.width = Width;
            image.height = Height;
            image.colourSpace = Fenrir::ColourSpace::Linear;
            for (std::uint32_t y = 0; y < Height; ++y)
            {
                for (std::uint32_t x = 0; x < Width; ++x)
                {
                    const float t = static_cast<float>(x) / static_cast<float>(Width - 1);
                    const float roughness = SmoothestFloor + (RoughestFloor - SmoothestFloor) * t * t;
                    const auto green = static_cast<std::uint8_t>(roughness * 255.0f + 0.5f);
                    for (const std::uint8_t channel : {std::uint8_t{0}, green, std::uint8_t{0}, std::uint8_t{255}})
                    {
                        image.pixels.push_back(std::byte{channel});
                    }
                }
            }
            return image;
        }

        /** @brief The floor, the walls and the plinths: a dark room whose floor goes from polished to satin. */
        void SpawnRoom(Fenrir::AssetStore& store, Fenrir::Scene& scene, Fenrir::AssetHandle<Fenrir::Shader> lit,
                       Fenrir::AssetHandle<Fenrir::Mesh> cube)
        {
            Fenrir::EntityList& entities = scene.GetEntityList();
            const auto floor = LitMaterial(store, lit, scene, {0.03f, 0.03f, 0.035f, 1.0f}, 0.0f, 1.0f);
            store.SetTexture(floor, "metallicRoughnessTexture", store.Add<Fenrir::Image>(RoughnessRamp(), scene));
            Box(entities, cube, floor, {0.0f, -0.1f, 0.0f}, {2.0f * HalfWidth, 0.2f, 2.0f * HalfDepth});

            const auto wall = LitMaterial(store, lit, scene, {0.06f, 0.06f, 0.065f, 1.0f}, 0.0f, 0.85f);
            Box(entities, cube, wall, {0.0f, RoomHeight * 0.5f, -HalfDepth}, {2.0f * HalfWidth, RoomHeight, 0.2f});
            Box(entities, cube, wall, {-HalfWidth, RoomHeight * 0.5f, 0.0f}, {0.2f, RoomHeight, 2.0f * HalfDepth});
            Box(entities, cube, wall, {HalfWidth, RoomHeight * 0.5f, 0.0f}, {0.2f, RoomHeight, 2.0f * HalfDepth});
            Box(entities, cube, wall, {0.0f, RoomHeight, 0.0f}, {2.0f * HalfWidth, 0.2f, 2.0f * HalfDepth});

            const auto plinth = LitMaterial(store, lit, scene, {0.85f, 0.85f, 0.82f, 1.0f}, 0.0f, 0.35f);
            for (const Exhibit& exhibit : Exhibits)
            {
                Box(entities, cube, plinth, {exhibit.x, PlinthHeight * 0.5f, -1.5f},
                    {PlinthSide, PlinthHeight, PlinthSide});
            }
        }

        /** @brief The radiance a light's slab glows with: its colour times its intensity. */
        Fenrir::Colour Radiance(const Fenrir::RectLight& light)
        {
            return {light.colour.r * light.intensity, light.colour.g * light.intensity,
                    light.colour.b * light.intensity, 1.0f};
        }

        /**
         * @brief A rect light at `position`, turned by `rotation`, with a thin unlit slab of its radiance and size
         * where it is, so the light shows as the shape it lights with.
         */
        ShowroomLight AddLight(Fenrir::AssetStore& store, Fenrir::Scene& scene,
                               Fenrir::AssetHandle<Fenrir::Shader> unlit, Fenrir::AssetHandle<Fenrir::Mesh> cube,
                               const char* name, Vec3 position, Quat rotation, const Fenrir::RectLight& light)
        {
            Fenrir::EntityList& entities = scene.GetEntityList();
            ShowroomLight out;
            out.name = name;
            out.light = entities.CreateEntity();
            Fenrir::Transform& transform = out.light.AddComponent<Fenrir::Transform>();
            transform.pos = position;
            transform.rot = rotation;
            out.light.AddComponent<Fenrir::RectLight>(light);
            out.glow = store.CreateMaterial(unlit, scene);
            store.SetColour(out.glow, "baseColour", Radiance(light));
            out.slab = Box(entities, cube, out.glow, position, {light.width, light.height, 0.02f});
            out.slab.GetComponent<Fenrir::Transform>().rot = rotation;
            return out;
        }

        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            ShowroomState state;
            // A dark room: the rect lights are what shows, and the floor's reflections of them.
            scene.AddResource<Fenrir::Sky>(Fenrir::ColourSky{{0.003f, 0.003f, 0.004f, 1.0f}});

            const auto cube = store.Load<Fenrir::Mesh>("cube.obj#Mesh0", scene);
            if (!cube.Ok())
            {
                app.Logger()->Error("{}: the cube did not load from the asset root", SceneName);
                scene.AddResource<ShowroomState>(state);
                return;
            }
            const auto lit = store.Load<Fenrir::Shader>("builtin:lit", scene).handle;
            const auto unlit = store.Load<Fenrir::Shader>("builtin:unlit", scene).handle;
            SpawnRoom(store, scene, lit, cube.handle);

            // Each light faces its entity's -Z: the room's front, turned half round, for those on the back wall.
            const Quat facingFront = Fenrir::Math::AngleAxis(Pi, Vec3(0.0f, 1.0f, 0.0f));
            state.lights.push_back(AddLight(store, scene, unlit, cube.handle, "Neon strip",
                                            {0.0f, 0.5f, -HalfDepth + 0.15f}, facingFront,
                                            {{1.0f, 0.08f, 0.55f, 1.0f}, 12.0f, 8.0f, 0.06f, false, 12.0f}));
            state.lights.push_back(AddLight(store, scene, unlit, cube.handle, "Panel",
                                            {-3.5f, 2.4f, -HalfDepth + 0.15f}, facingFront,
                                            {{1.0f, 0.92f, 0.8f, 1.0f}, 4.0f, 2.4f, 1.2f, false, 12.0f}));
            // Hung side on from the ceiling, lighting the room's two halves, cyan on both faces.
            state.lights.push_back(AddLight(store, scene, unlit, cube.handle, "Two-sided sign", {3.0f, 2.6f, 0.5f},
                                            Fenrir::Math::AngleAxis(Pi * 0.5f, Vec3(0.0f, 1.0f, 0.0f)),
                                            {{0.1f, 0.85f, 1.0f, 1.0f}, 5.0f, 1.6f, 0.7f, true, 10.0f}));

            // The helmets on their plinths, when fetched. The room and its lights show either way.
            std::vector<std::string> paths;
            for (const Exhibit& exhibit : Exhibits)
            {
                if (const std::optional<std::string> path = FetchedAsset(exhibit.path))
                {
                    paths.push_back(*path);
                }
            }
            if (paths.size() != Exhibits.size())
            {
                state.missingAssets = true;
                app.Logger()->Info("{}: the fetched demo assets are missing, so the plinths are bare "
                                   "(FENRIR_FETCH_DEMO_ASSETS, ADR-0052)",
                                   SceneName);
                scene.AddResource<ShowroomState>(state);
                return;
            }
            for (std::size_t i = 0; i < Exhibits.size(); ++i)
            {
                const auto model = store.Load<Fenrir::Model>(paths.at(i), scene);
                if (!model.Ok())
                {
                    app.Logger()->Error("{}: {} did not load from the cache", SceneName, Exhibits.at(i).path);
                    continue;
                }
                Fenrir::Entity root = Fenrir::SpawnModel(app, scene, model.handle);
                if (root.IsValid())
                {
                    auto& transform = root.GetComponent<Fenrir::Transform>();
                    transform.pos = {Exhibits.at(i).x, PlinthHeight + Exhibits.at(i).lift, -1.5f};
                    transform.scale = Vec3(Exhibits.at(i).scale);
                }
            }
            scene.AddResource<ShowroomState>(state);
        }

        /** @brief The slab of `light` sized, glowing and shown as its RectLight now is. */
        void Follow(Fenrir::AssetStore& store, ShowroomLight& light)
        {
            if (!light.light.IsValid() || !light.slab.IsValid())
            {
                return;
            }
            const Fenrir::RectLight& rect = light.light.GetComponent<Fenrir::RectLight>();
            light.slab.GetComponent<Fenrir::Transform>().scale = {rect.width, rect.height, 0.02f};
            store.SetColour(light.glow, "baseColour", Radiance(rect));
        }

        void Update(Fenrir::App& app, Fenrir::Scene& scene)
        {
            ShowroomState* state = scene.GetResource<ShowroomState>();
            if (state == nullptr)
            {
                return;
            }
            if (state->missingAssets)
            {
                ShowMissingAssets(scene, SceneName);
            }
            const Fenrir::Keyboard& keyboard = app.GetResource<Fenrir::Keyboard>();
            const float before = SizeSteps.at(static_cast<std::size_t>(state->sizeStep));
            if (keyboard.JustPressed(Fenrir::Key::Z))
            {
                state->sizeStep = (state->sizeStep + 1) % static_cast<int>(SizeSteps.size());
                app.Logger()->Info("Z: every light at {}x its size",
                                   SizeSteps.at(static_cast<std::size_t>(state->sizeStep)));
            }
            if (keyboard.JustPressed(Fenrir::Key::V))
            {
                for (ShowroomLight& light : state->lights)
                {
                    if (light.light.IsValid())
                    {
                        bool& twoSided = light.light.GetComponent<Fenrir::RectLight>().twoSided;
                        twoSided = !twoSided;
                    }
                }
                app.Logger()->Info("V: every light's sidedness flipped");
            }
            // Z resizes every light by the step's ratio, so sizes set in the panel keep their proportions.
            const float ratio = SizeSteps.at(static_cast<std::size_t>(state->sizeStep)) / before;

            RunLightingPanel(app, scene, state->lighting, [&] {
                for (ShowroomLight& light : state->lights)
                {
                    if (!light.light.IsValid())
                    {
                        continue;
                    }
                    Fenrir::RectLight& rect = light.light.GetComponent<Fenrir::RectLight>();
                    ImGui::PushID(light.name);
                    ImGui::SeparatorText(light.name);
                    ImGui::SliderFloat("Width", &rect.width, 0.02f, 8.0f);
                    ImGui::SliderFloat("Height", &rect.height, 0.02f, 4.0f);
                    ImGui::SliderFloat("Intensity", &rect.intensity, 0.0f, 30.0f);
                    ImGui::Checkbox("Two-sided (V)", &rect.twoSided);
                    ImGui::PopID();
                }
            });

            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            for (ShowroomLight& light : state->lights)
            {
                if (light.light.IsValid() && ratio != 1.0f)
                {
                    Fenrir::RectLight& rect = light.light.GetComponent<Fenrir::RectLight>();
                    rect.width *= ratio;
                    rect.height *= ratio;
                }
                Follow(store, light);
            }
        }
    } // namespace

    FenrirDemo::DemoScene Showroom()
    {
        FenrirDemo::DemoScene scene;
        scene.name = SceneName;
        scene.keyHelp = LightingKeyHelp(false) + " Z: every light half, as set, or twice its size. V: flip every "
                                                 "light's sidedness.";
        scene.keys = {Fenrir::Key::T, Fenrir::Key::E, Fenrir::Key::B, Fenrir::Key::Z,
                      Fenrir::Key::V, Fenrir::Key::Z, Fenrir::Key::V, Fenrir::Key::Z};
        // At the front of the room, a little above head height, looking down across the floor to the back wall.
        scene.camera.position = {0.0f, 2.0f, 4.5f};
        scene.camera.pitch = -0.2f;
        scene.spawn = Spawn;
        scene.update = Update;
        return scene;
    }
} // namespace LightingDemo
