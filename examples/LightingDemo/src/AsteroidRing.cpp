#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <vector>

#include <imgui.h>

#include "DemoMaterials.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/ImageData.hpp"
#include "FenrirAssets/InstanceListData.hpp"
#include "FenrirAssets/MeshData.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirRenderer/Renderer.hpp"
#include "FenrirScene/Scene.hpp"
#include "FetchedAssets.hpp"
#include "LightingPanel.hpp"
#include "LightingScenes.hpp"
#include "MeshTools.hpp"

namespace LightingDemo
{
    namespace
    {
        using Fenrir::Math::Vec3;

        constexpr const char* SceneName = "Asteroid ring";
        constexpr float Pi = std::numbers::pi_v<float>;

        /** @brief The planet's radius, and the ring's inner and outer radii and half its thickness, in metres. */
        constexpr float PlanetRadius = 20.0f;
        constexpr float RingInner = 30.0f;
        constexpr float RingOuter = 55.0f;
        constexpr float RingHalfThickness = 0.8f;
        /** @brief The most rocks in the ring, split evenly between its kinds; the panel shows any number up to it. */
        constexpr int MaxRingRocks = 100000;
        /** @brief The counts N steps through. */
        constexpr std::array<int, 3> RingSteps{10000, 50000, 100000};
        /** @brief How many rock entities the field beside the ring holds, merged automatically (ADR-0051). */
        constexpr int FieldRocks = 5000;
        /** @brief The field's centre and radius, and the station's centre: off the ring's outer edge. */
        const Vec3 FieldCentre{60.0f, 3.0f, 8.0f};
        constexpr float FieldRadius = 7.0f;
        const Vec3 StationCentre{62.0f, 4.0f, 19.0f};
        /** @brief The ring's turn, in radians a second. */
        constexpr float RingSpin = 0.01f;

        /** @brief A Poly Haven moon rock, fetched (ADR-0052): its glTF's manifest path. */
        constexpr std::array<const char*, 3> RockModels{"moon_rock_02/moon_rock_02_1k.gltf",
                                                        "moon_rock_03/moon_rock_03_1k.gltf",
                                                        "moon_rock_07/moon_rock_07_1k.gltf"};
        constexpr const char* PlanetTexture = "planets/2k_jupiter.jpg";

        /**
         * @brief How a rock's levels are made: halving to seven levels and 32 to 52 triangles, the coarsest the specks
         * that are most of the ring, with its UV seams free to collapse, as `LodSettings` leaves them (a scanned
         * rock's islands lock it at a few hundred triangles otherwise).
         */
        constexpr Fenrir::LodSettings RockLods{.ratio = 0.5f, .maxLevels = 7, .minTriangles = 32};
        /** @brief The level-of-detail errors L steps through, in pixels; the renderer's default first. */
        constexpr std::array<float, 4> LodErrorSteps{1.0f, 2.0f, 4.0f, 0.5f};

        /**
         * @brief One kind of rock: its mesh at full detail with its generated levels (ADR-0057), which the field and
         * the ring both draw, its material, and the scale that makes its mesh a metre across.
         */
        struct RockKind
        {
            Fenrir::AssetHandle<Fenrir::Mesh> mesh;
            Fenrir::AssetHandle<Fenrir::Material> material;
            float toMetre = 1.0f;
        };

        /**
         * @brief The renderer's culling as the panel and keys set it (ADR-0057): the GPU test, occlusion culling, the
         * freeze and the level-of-detail error. The renderer's, like merging, so a change is applied once, not every
         * frame.
         */
        struct CullingControls
        {
            bool gpuCulling = true;
            bool occlusion = true;
            bool frozen = false;
            float lodErrorPixels = LodErrorSteps.front();

            friend bool operator==(const CullingControls&, const CullingControls&) = default;

            /** @brief Sets all four on the renderer; setting one again as it is changes nothing. */
            void Apply(Fenrir::App& app) const
            {
                Fenrir::Renderer::SetGpuCulling(app, gpuCulling);
                Fenrir::Renderer::SetOcclusionCulling(app, occlusion);
                Fenrir::Renderer::SetCullingFrozen(app, frozen);
                // No least size: a rock that projects under a pixel still draws, at its coarsest level.
                Fenrir::Renderer::SetLodSelection(app, lodErrorPixels, 0.0f);
            }
        };

        /** @brief The ring's lists and what the panel and keys have set, kept as a scene resource. */
        struct AsteroidRingState
        {
            LightingSettings lighting;
            /** @brief Every kind's full list, of which the first `count` split between them are drawn. */
            std::vector<Fenrir::InstanceListData> lists;
            std::vector<Fenrir::AssetHandle<Fenrir::InstanceList>> handles;
            Fenrir::Entity ring;
            int count = MaxRingRocks;
            /** @brief The count the lists were last cut to. */
            int listedCount = MaxRingRocks;
            /** @brief Whether every fetched asset loaded; the scene says so on screen when not. */
            bool fetched = false;
            bool merging = true;
            bool spinning = true;
            CullingControls culling;
            float angle = 0.0f;
            /** @brief The frame time, smoothed over about a second, in seconds. */
            float frameTime = 0.0f;
        };

        /**
         * @brief Gives `mesh` its levels of detail once, in the store (ADR-0057): the ring's rocks are a few pixels
         * across and draw the coarsest, the field's are close enough to draw full detail. The store refuses nothing
         * GenerateLods makes, so the replacement is not checked.
         */
        void AddLevels(Fenrir::AssetStore& store, Fenrir::AssetHandle<Fenrir::Mesh> mesh)
        {
            if (const Fenrir::MeshData* data = store.TryGet(mesh))
            {
                store.Replace(mesh, Fenrir::GenerateLods(*data, RockLods));
            }
        }

        /** @brief The scale that fits `mesh` into a box a metre on its longest side. */
        float UnitScale(const Fenrir::AssetStore& store, Fenrir::AssetHandle<Fenrir::Mesh> mesh)
        {
            const Fenrir::MeshData* data = store.TryGet(mesh);
            if (data == nullptr)
            {
                return 1.0f;
            }
            const Vec3 size = data->bounds.max - data->bounds.min;
            const float longest = std::max({size.x, size.y, size.z});
            return longest > 0.0f ? 1.0f / longest : 1.0f;
        }

        /**
         * @brief A starfield panorama made here, linear RGBA32F: a faint blue-black, a soft band of milky light round a
         * tilted great circle, and stars of a few pixels each, most dim and a few bright enough to bloom.
         */
        Fenrir::ImageData Starfield(std::uint32_t width)
        {
            const std::uint32_t height = width / 2;
            std::vector<float> texels(std::size_t{width} * height * 4, 0.0f);
            const Vec3 bandPole = glm::normalize(Vec3(0.3f, 1.0f, 0.5f));
            for (std::uint32_t row = 0; row < height; ++row)
            {
                const float theta = Pi * (static_cast<float>(row) + 0.5f) / static_cast<float>(height);
                for (std::uint32_t column = 0; column < width; ++column)
                {
                    const float phi = 2.0f * Pi * (static_cast<float>(column) + 0.5f) / static_cast<float>(width);
                    const Vec3 direction{std::sin(theta) * std::cos(phi), std::cos(theta),
                                         std::sin(theta) * std::sin(phi)};
                    const float band = std::exp(-std::pow(glm::dot(direction, bandPole) / 0.18f, 2.0f));
                    float* texel = &texels[(std::size_t{row} * width + column) * 4];
                    texel[0] = 0.0006f + 0.010f * band;
                    texel[1] = 0.0007f + 0.009f * band;
                    texel[2] = 0.0012f + 0.012f * band;
                    texel[3] = 1.0f;
                }
            }
            // Stars thin towards the poles as the panorama's rows shrink there, so they spread evenly on the sphere.
            std::mt19937 random(7);
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            const std::size_t stars = std::size_t{width} * height / 700;
            for (std::size_t i = 0; i < stars; ++i)
            {
                const float y = unit(random) * 2.0f - 1.0f;
                const auto row = static_cast<std::uint32_t>(std::acos(y) / Pi * static_cast<float>(height - 1));
                const auto column = static_cast<std::uint32_t>(unit(random) * static_cast<float>(width - 1));
                // Mostly faint, rarely very bright: brightness falls as a power of a uniform draw.
                const float brightness = 0.02f + 4.0f * std::pow(unit(random), 24.0f);
                const float warmth = unit(random);
                const Vec3 tint = glm::mix(Vec3(0.7f, 0.8f, 1.0f), Vec3(1.0f, 0.85f, 0.6f), warmth);
                float* texel = &texels[(std::size_t{row} * width + column) * 4];
                texel[0] += tint.r * brightness;
                texel[1] += tint.g * brightness;
                texel[2] += tint.b * brightness;
            }
            Fenrir::ImageData image;
            image.width = width;
            image.height = height;
            image.format = Fenrir::ImageFormat::RGBA32F;
            image.colourSpace = Fenrir::ColourSpace::Linear;
            const auto bytes = std::as_bytes(std::span(texels));
            image.pixels.assign(bytes.begin(), bytes.end());
            return image;
        }

        /** @brief Where a rock is: its position, turn and scale. */
        struct RockPlacement
        {
            Vec3 position{0.0f};
            Fenrir::Math::Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
            Vec3 scale{1.0f};

            [[nodiscard]] Fenrir::Math::Mat4 Matrix() const
            {
                return Placement(position, rotation, scale);
            }
        };

        /** @brief A random rock at `position`: turned any way, `size` metres across, its mesh scaled by `toMetre`. */
        RockPlacement PlaceRock(std::mt19937& random, Vec3 position, float size, float toMetre)
        {
            std::uniform_real_distribution<float> symmetric(-1.0f, 1.0f);
            std::uniform_real_distribution<float> turn(0.0f, 2.0f * Pi);
            Vec3 axis{symmetric(random), symmetric(random), symmetric(random)};
            axis = glm::length(axis) > 0.01f ? glm::normalize(axis) : Vec3(0.0f, 1.0f, 0.0f);
            // A little squash along one axis, so copies of one mesh read as different rocks.
            const float squash = 0.7f + 0.3f * (symmetric(random) * 0.5f + 0.5f);
            return {position, Fenrir::Math::AngleAxis(turn(random), axis),
                    Vec3(size * toMetre, size * toMetre * squash, size * toMetre)};
        }

        /** @brief A grey-brown rock tint, near white so the rock's own texture shows. */
        Fenrir::Colour RockTint(std::mt19937& random)
        {
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            const float light = 0.55f + 0.45f * unit(random);
            const float warm = 0.1f * unit(random);
            return {light, light * (0.97f - warm * 0.4f), light * (0.93f - warm), 1.0f};
        }

        /** @brief Each kind's list of `MaxRingRocks` split between them, round the ring in a thin, lumpy disc. */
        std::vector<Fenrir::InstanceListData> RingLists(const std::vector<RockKind>& kinds)
        {
            std::mt19937 random(13);
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            std::normal_distribution<float> thickness(0.0f, RingHalfThickness * 0.5f);
            std::vector<Fenrir::InstanceListData> lists(kinds.size());
            for (int i = 0; i < MaxRingRocks; ++i)
            {
                const std::size_t kind = static_cast<std::size_t>(i) % kinds.size();
                // Denser towards the middle of the ring, thinning to its edges.
                const float across = 0.5f * (unit(random) + unit(random));
                const float radius = RingInner + (RingOuter - RingInner) * across;
                const float angle = 2.0f * Pi * unit(random);
                const Vec3 position{std::cos(angle) * radius, thickness(random), std::sin(angle) * radius};
                // Mostly pebbles, a few boulders: size falls as a power of a uniform draw.
                const float size = 0.08f + 0.9f * std::pow(unit(random), 4.0f);
                lists[kind].transforms.push_back(PlaceRock(random, position, size, kinds[kind].toMetre).Matrix());
                lists[kind].colours.push_back(RockTint(random));
            }
            return lists;
        }

        /** @brief About `FieldRocks` rock entities in a ball beside the ring, which automatic instancing merges. */
        void SpawnField(Fenrir::Scene& scene, const std::vector<RockKind>& kinds,
                        const std::vector<Fenrir::AssetHandle<Fenrir::Material>>& tints)
        {
            std::mt19937 random(29);
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            std::uniform_real_distribution<float> symmetric(-1.0f, 1.0f);
            Fenrir::EntityList& entities = scene.GetEntityList();
            for (int i = 0; i < FieldRocks; ++i)
            {
                Vec3 offset;
                do
                {
                    offset = {symmetric(random), symmetric(random) * 0.6f, symmetric(random)};
                } while (glm::length(offset) > 1.0f);
                const std::size_t kind = static_cast<std::size_t>(i) % kinds.size();
                const float size = 0.1f + 0.7f * std::pow(unit(random), 3.0f);
                const RockPlacement placement =
                    PlaceRock(random, FieldCentre + offset * FieldRadius, size, kinds[kind].toMetre);
                Fenrir::Entity rock = entities.CreateEntity();
                rock.AddComponent<Fenrir::Transform>(
                    Fenrir::Transform(placement.position, placement.rotation, placement.scale));
                // A few tints per kind, so the field's rocks still merge into a handful of draws.
                const auto material = tints[kind * 3 + static_cast<std::size_t>(i / 3) % 3];
                rock.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{kinds[kind].mesh, material});
            }
        }

        /**
         * @brief The station beside the field: a hull, a ring of modules on spokes, glowing windows and beacons, and
         * coloured point lights that light the rocks nearest it.
         */
        void SpawnStation(Fenrir::AssetStore& store, Fenrir::Scene& scene, Fenrir::AssetHandle<Fenrir::Mesh> cube)
        {
            Fenrir::EntityList& entities = scene.GetEntityList();
            const auto hull = LitMaterial(store, scene, {0.7f, 0.72f, 0.75f, 1.0f}, 0.9f, 0.35f);
            const auto dark = LitMaterial(store, scene, {0.08f, 0.08f, 0.09f, 1.0f}, 0.6f, 0.5f);
            const auto window =
                LitMaterial(store, scene, {0.05f, 0.05f, 0.05f, 1.0f}, 0.0f, 0.2f, {1.0f, 0.75f, 0.45f, 1.0f}, 6.0f);
            Box(entities, cube, hull, StationCentre, {1.2f, 5.0f, 1.2f});
            Box(entities, cube, window, StationCentre + Vec3(0.0f, 0.0f, 0.61f), {0.3f, 4.0f, 0.02f});
            constexpr int Modules = 6;
            constexpr float Reach = 4.0f;
            const std::array<Fenrir::Colour, 3> beacons{
                {{0.3f, 0.6f, 1.0f, 1.0f}, {1.0f, 0.4f, 0.2f, 1.0f}, {0.4f, 1.0f, 0.6f, 1.0f}}};
            for (int i = 0; i < Modules; ++i)
            {
                const float angle = 2.0f * Pi * static_cast<float>(i) / Modules;
                const Vec3 out{std::cos(angle), 0.0f, std::sin(angle)};
                Box(entities, cube, dark, StationCentre + out * (Reach * 0.5f), Vec3(0.2f) + glm::abs(out) * Reach);
                const Vec3 module = StationCentre + out * Reach;
                Box(entities, cube, hull, module, {1.4f, 1.0f, 1.4f});
                Box(entities, cube, window, module + Vec3(0.0f, 0.51f, 0.0f), {1.0f, 0.02f, 1.0f});
                // A beacon on each module, lighting the rocks round it.
                const Fenrir::Colour colour = beacons.at(static_cast<std::size_t>(i) % beacons.size());
                const auto glow = LitMaterial(store, scene, Fenrir::Colour::Black, 0.0f, 0.5f, colour, 40.0f);
                const Vec3 beacon = module + out * 1.0f + Vec3(0.0f, 0.8f, 0.0f);
                Box(entities, cube, glow, beacon, Vec3(0.18f));
                Fenrir::Entity light = entities.CreateEntity();
                light.AddComponent<Fenrir::Transform>().pos = beacon;
                light.AddComponent<Fenrir::PointLight>(Fenrir::PointLight{colour, 60.0f, 18.0f, false});
            }
        }

        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            AsteroidRingState state;
            state.lighting.panoramas.push_back(
                {"Procedural starfield", store.Add<Fenrir::Image>(Starfield(2048), scene)});
            state.lighting.look.bloom.intensity = 0.06f;
            // The whole ring, from where the camera starts, so the planet's shadow falls across it (ADR-0050).
            state.lighting.sunShadows.shadowDistance = 200.0f;

            const auto cube = store.Load<Fenrir::Mesh>("cube.obj#Mesh0", scene);
            const auto sphere = store.Load<Fenrir::Mesh>("sphere.obj#Mesh0", scene);
            if (!cube.Ok() || !sphere.Ok())
            {
                app.Logger()->Error("{}: the cube or the sphere did not load from the asset root", SceneName);
                scene.AddResource<AsteroidRingState>(std::move(state));
                return;
            }

            // The Poly Haven rocks and the planet's texture, fetched; the committed sphere and cube stand in without.
            std::vector<RockKind> kinds;
            for (const char* model : RockModels)
            {
                const std::optional<std::string> path = FetchedAsset(model);
                if (!path)
                {
                    break;
                }
                const auto mesh = store.Load<Fenrir::Mesh>(*path + "#Mesh0", scene);
                const auto material = store.Load<Fenrir::Material>(*path + "#Material0", scene);
                if (mesh.Ok() && material.Ok())
                {
                    AddLevels(store, mesh.handle);
                    kinds.push_back({mesh.handle, material.handle, UnitScale(store, mesh.handle)});
                }
            }
            const std::optional<std::string> planetPath = FetchedAsset(PlanetTexture);
            const bool fetched = kinds.size() == RockModels.size() && planetPath.has_value();
            if (!fetched)
            {
                const auto rock = LitMaterial(store, scene, {0.45f, 0.42f, 0.4f, 1.0f}, 0.0f, 0.9f);
                // The sphere gets levels (the planet, one copy of it, draws the finest up close); the cube's hard
                // edges are seams, so it keeps its twelve triangles.
                AddLevels(store, sphere.handle);
                AddLevels(store, cube.handle);
                kinds = {{sphere.handle, rock, UnitScale(store, sphere.handle)},
                         {cube.handle, rock, UnitScale(store, cube.handle)}};
                app.Logger()->Info(
                    "{}: the rocks and planet are fetched assets (FENRIR_FETCH_DEMO_ASSETS); spheres and "
                    "cubes stand in",
                    SceneName);
            }

            state.fetched = fetched;
            Fenrir::EntityList& entities = scene.GetEntityList();
            Fenrir::Entity sun = entities.CreateEntity();
            sun.AddComponent<Fenrir::Transform>().rot =
                glm::quatLookAt(glm::normalize(Vec3(-1.0f, -0.25f, -0.4f)), Vec3(0.0f, 1.0f, 0.0f));
            sun.AddComponent<Fenrir::DirectionalLight>(Fenrir::DirectionalLight{{1.0f, 0.96f, 0.9f, 1.0f}, 2.5f});

            const auto planetMaterial = LitMaterial(store, scene, {0.8f, 0.65f, 0.5f, 1.0f}, 0.0f, 0.85f);
            if (planetPath)
            {
                const auto texture = store.Load<Fenrir::Image>(*planetPath, scene);
                if (texture.Ok())
                {
                    store.SetColour(planetMaterial, "baseColour", Fenrir::Colour::White);
                    store.SetTexture(planetMaterial, "baseColourTexture", texture.handle);
                }
            }
            Fenrir::Entity planet = entities.CreateEntity();
            Fenrir::Transform& planetTransform = planet.AddComponent<Fenrir::Transform>();
            planetTransform.scale = Vec3(2.0f * PlanetRadius * UnitScale(store, sphere.handle));
            planetTransform.rot = Fenrir::Math::AngleAxis(0.4f, Vec3(0.0f, 0.0f, 1.0f));
            planet.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{sphere.handle, planetMaterial});

            // One InstancedMeshRenderer a kind on one ring entity: turning the entity turns every copy, uploading
            // nothing.
            state.lists = RingLists(kinds);
            state.ring = entities.CreateEntity();
            state.ring.AddComponent<Fenrir::Transform>().rot = Fenrir::Math::AngleAxis(0.12f, Vec3(1.0f, 0.0f, 0.0f));
            for (std::size_t kind = 0; kind < kinds.size(); ++kind)
            {
                const auto list = store.Add<Fenrir::InstanceList>(state.lists[kind], scene);
                state.handles.push_back(list);
                Fenrir::Entity layer = entities.CreateEntity();
                layer.AddComponent<Fenrir::Transform>();
                // The ring takes the planet's shadow but casts none: 100k rocks in four cascades would cost four
                // times the ring again for specks of shadow on each other.
                layer.AddComponent<Fenrir::InstancedMeshRenderer>(
                    Fenrir::InstancedMeshRenderer{kinds[kind].mesh, kinds[kind].material, list, false});
                state.ring.AddChild(layer);
            }

            // The field's rocks share a few tinted copies of each kind's material.
            std::vector<Fenrir::AssetHandle<Fenrir::Material>> tints;
            std::mt19937 random(31);
            for (const RockKind& kind : kinds)
            {
                for (int i = 0; i < 3; ++i)
                {
                    const auto tinted = fetched ? store.CloneMaterial(kind.material, scene)
                                                : LitMaterial(store, scene, {0.45f, 0.42f, 0.4f, 1.0f}, 0.0f, 0.9f);
                    store.SetColour(tinted, "baseColour", RockTint(random));
                    tints.push_back(tinted);
                }
            }
            SpawnField(scene, kinds, tints);
            SpawnStation(store, scene, cube.handle);
            // Culling as the panel starts: on, not frozen and at the renderer's default error, whatever another visit
            // left them as.
            state.culling.Apply(app);
            scene.AddResource<AsteroidRingState>(std::move(state));
        }

        void Update(Fenrir::App& app, Fenrir::Scene& scene)
        {
            AsteroidRingState* state = scene.GetResource<AsteroidRingState>();
            if (state == nullptr)
            {
                return;
            }
            if (!state->fetched)
            {
                ShowMissingAssets(scene, SceneName);
            }
            const Fenrir::Keyboard& keyboard = app.GetResource<Fenrir::Keyboard>();
            if (keyboard.JustPressed(Fenrir::Key::I))
            {
                state->merging = !state->merging;
                app.Logger()->Info("I: automatic instancing {}", state->merging ? "on" : "off");
            }
            if (keyboard.JustPressed(Fenrir::Key::N))
            {
                const auto next =
                    std::find_if(RingSteps.begin(), RingSteps.end(), [&](int step) { return step > state->count; });
                state->count = next == RingSteps.end() ? RingSteps.front() : *next;
                app.Logger()->Info("N: {} rocks in the ring", state->count);
            }
            if (keyboard.JustPressed(Fenrir::Key::R))
            {
                state->spinning = !state->spinning;
            }
            CullingControls culling = state->culling;
            if (keyboard.JustPressed(Fenrir::Key::C))
            {
                culling.gpuCulling = !culling.gpuCulling;
                app.Logger()->Info("C: GPU culling {}", culling.gpuCulling ? "on" : "off");
            }
            if (keyboard.JustPressed(Fenrir::Key::V))
            {
                culling.occlusion = !culling.occlusion;
                app.Logger()->Info("V: occlusion culling {}", culling.occlusion ? "on" : "off");
            }
            if (keyboard.JustPressed(Fenrir::Key::F))
            {
                culling.frozen = !culling.frozen;
                app.Logger()->Info("F: culling {}", culling.frozen ? "frozen" : "following the camera");
            }
            if (keyboard.JustPressed(Fenrir::Key::L))
            {
                // From a slider value between the steps, the first step.
                const auto step = std::find(LodErrorSteps.begin(), LodErrorSteps.end(), culling.lodErrorPixels);
                const auto next =
                    step == LodErrorSteps.end() || step + 1 == LodErrorSteps.end() ? LodErrorSteps.begin() : step + 1;
                culling.lodErrorPixels = *next;
                app.Logger()->Info("L: a level of detail may depart by {} pixels", culling.lodErrorPixels);
            }

            const Fenrir::RendererStats stats = Fenrir::Renderer::Stats(app);
            const auto delta = static_cast<float>(app.GetTime().deltaTime);
            state->frameTime = state->frameTime == 0.0f ? delta : glm::mix(state->frameTime, delta, 0.05f);
            RunLightingPanel(app, scene, state->lighting, [&] {
                ImGui::SliderInt("Ring rocks (N)", &state->count, 1000, MaxRingRocks);
                ImGui::Checkbox("Automatic instancing (I)", &state->merging);
                ImGui::Checkbox("Ring turning (R)", &state->spinning);
                ImGui::Text("Ring rocks: %d in %d lists; field rocks: %d entities", state->listedCount,
                            static_cast<int>(state->handles.size()), FieldRocks);
                ImGui::Text("Draws: %u  Instances: %u  Drawn: %u  Uploads: %u", stats.draws, stats.instances,
                            stats.instancesDrawn, stats.uploads);
                ImGui::Text("Frame: %.2f ms (%.0f fps)", state->frameTime * 1000.0f,
                            state->frameTime > 0.0f ? 1.0f / state->frameTime : 0.0f);
                ImGui::SeparatorText("Culling");
                ImGui::Checkbox("GPU culling (C)", &culling.gpuCulling);
                ImGui::Checkbox("Occlusion culling (V)", &culling.occlusion);
                ImGui::Checkbox("Freeze culling (F)", &culling.frozen);
                ImGui::SliderFloat("Level-of-detail error, pixels (L)", &culling.lodErrorPixels, 0.25f, 8.0f, "%.2f",
                                   ImGuiSliderFlags_Logarithmic);
                ImGui::Text("Drawn by level: %u  %u  %u  %u  %u  %u  %u  %u", stats.drawnByLod[0], stats.drawnByLod[1],
                            stats.drawnByLod[2], stats.drawnByLod[3], stats.drawnByLod[4], stats.drawnByLod[5],
                            stats.drawnByLod[6], stats.drawnByLod[7]);
                ImGui::Text("Drawn by phase: %u visible last frame, %u shown by the Hi-Z pyramid",
                            stats.drawnByPhase[0], stats.drawnByPhase[1]);
                ImGui::TextUnformatted("Drawn is what the GPU kept, two frames behind; off, every copy draws level 0.");
            });

            Fenrir::Renderer::SetAutomaticInstancing(app, state->merging);
            if (culling != state->culling)
            {
                state->culling = culling;
                culling.Apply(app);
            }
            // A new count cuts each list again: Replace bumps its revision, so the renderer uploads it once more.
            if (state->count != state->listedCount && !state->handles.empty())
            {
                Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
                const std::size_t kinds = state->handles.size();
                for (std::size_t kind = 0; kind < kinds; ++kind)
                {
                    const std::size_t share = (static_cast<std::size_t>(state->count) + kinds - 1 - kind) / kinds;
                    store.Replace(state->handles[kind], FirstCopies(state->lists[kind], share));
                }
                state->listedCount = state->count;
            }
            if (state->spinning && state->ring.IsValid())
            {
                state->angle += RingSpin * static_cast<float>(app.GetTime().deltaTime);
                state->ring.GetComponent<Fenrir::Transform>().rot =
                    Fenrir::Math::AngleAxis(0.12f, Vec3(1.0f, 0.0f, 0.0f)) *
                    Fenrir::Math::AngleAxis(state->angle, Vec3(0.0f, 1.0f, 0.0f));
            }
        }
    } // namespace

    FenrirDemo::DemoScene AsteroidRing()
    {
        FenrirDemo::DemoScene scene;
        scene.name = SceneName;
        scene.keyHelp =
            LightingKeyHelp(false) +
            " N: 10k, 50k or 100k ring rocks. I: automatic instancing on or off. R: the ring turning. C: GPU "
            "culling off or on. V: occlusion culling off or on. F: culling frozen or following. L: the next "
            "level-of-detail error (1, 2, 4, 0.5 pixels).";
        // G through every debug view (the occlusion, the cascades, the shadow atlas, back), with the sun's shadows off
        // and on again (H) between; then culling off and on, occlusion culling off and on, frozen and following, and L
        // round its four errors, each control left as it started for the scenes after.
        scene.keys = {Fenrir::Key::T, Fenrir::Key::B, Fenrir::Key::I, Fenrir::Key::N, Fenrir::Key::I, Fenrir::Key::R,
                      Fenrir::Key::N, Fenrir::Key::N, Fenrir::Key::G, Fenrir::Key::G, Fenrir::Key::G, Fenrir::Key::H,
                      Fenrir::Key::G, Fenrir::Key::H, Fenrir::Key::C, Fenrir::Key::C, Fenrir::Key::V, Fenrir::Key::V,
                      Fenrir::Key::F, Fenrir::Key::F, Fenrir::Key::L, Fenrir::Key::L, Fenrir::Key::L, Fenrir::Key::L};
        // Beyond the station, looking past it and the field across the ring to the planet.
        scene.camera.position = {74.0f, 9.0f, 34.0f};
        scene.camera.yaw = 1.08f;
        scene.camera.pitch = -0.12f;
        scene.spawn = Spawn;
        scene.update = Update;
        return scene;
    }
} // namespace LightingDemo
