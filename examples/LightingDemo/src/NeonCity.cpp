#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "DemoMaterials.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/InstanceListData.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirRenderer/Renderer.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirShaders/LightingDemoFacade.hpp"
#include "FenrirShaders/LightingDemoStreet.hpp"
#include "FetchedAssets.hpp"
#include "LightingPanel.hpp"
#include "LightingScenes.hpp"
#include "MeshTools.hpp"

namespace LightingDemo
{
    namespace
    {
        using Fenrir::Math::Mat4;
        using Fenrir::Math::Quat;
        using Fenrir::Math::Vec2;
        using Fenrir::Math::Vec3;

        constexpr const char* SceneName = "Neon city";
        const Vec3 Up{0.0f, 1.0f, 0.0f};

        // The city is a grid of square blocks, each three lots by three, between streets of two lanes. Avenues run
        // along X and streets along Z, each with its centre line at one of StreetLines.
        constexpr int Blocks = 5;
        constexpr int LotsPerSide = 3;
        constexpr float Lot = 12.0f;
        constexpr float BlockSide = Lot * LotsPerSide;
        constexpr float Sidewalk = 2.5f;
        /** @brief Kerb to kerb. */
        constexpr float Road = 11.0f;
        constexpr float Pitch = BlockSide + 2.0f * Sidewalk + Road;
        /** @brief Where the outermost street's far kerb is, from the city's centre. */
        constexpr float CityHalf = Pitch * Blocks * 0.5f + Road * 0.5f;
        constexpr float KerbHeight = 0.15f;
        /** @brief A lane's centre from its road's centre line. */
        constexpr float LaneOffset = 2.6f;
        constexpr int CarsPerLane = 3;

        /** @brief The fetched files: the asphalt's base colour, normal and ARM maps, and the night panorama. */
        constexpr std::array<const char*, 3> AsphaltMaps{"asphalt_01/asphalt_01_diff_2k.jpg",
                                                         "asphalt_01/asphalt_01_nor_gl_2k.jpg",
                                                         "asphalt_01/asphalt_01_arm_2k.jpg"};
        constexpr const char* Panorama = "panoramas/kloppenheim_07_puresky_2k.hdr";

        /** @brief The neon's colours, linear. */
        constexpr std::array<Fenrir::Colour, 8> Neon{{{1.0f, 0.08f, 0.5f, 1.0f},
                                                      {0.08f, 0.8f, 1.0f, 1.0f},
                                                      {0.55f, 0.15f, 1.0f, 1.0f},
                                                      {1.0f, 0.4f, 0.04f, 1.0f},
                                                      {0.15f, 1.0f, 0.3f, 1.0f},
                                                      {1.0f, 0.06f, 0.06f, 1.0f},
                                                      {0.12f, 0.25f, 1.0f, 1.0f},
                                                      {1.0f, 0.8f, 0.12f, 1.0f}}};
        /** @brief The windows' light: warm tungsten, a neutral white and a cool fluorescent. */
        constexpr std::array<Fenrir::Colour, 3> WindowLight{
            {{1.0f, 0.62f, 0.32f, 1.0f}, {1.0f, 0.86f, 0.66f, 1.0f}, {0.7f, 0.85f, 1.0f, 1.0f}}};
        /** @brief The cars' paint: black, white, silver, red, dark blue and a taxi's yellow. */
        constexpr std::array<Fenrir::Colour, 6> Paint{{{0.02f, 0.02f, 0.02f, 1.0f},
                                                       {0.8f, 0.8f, 0.78f, 1.0f},
                                                       {0.45f, 0.46f, 0.48f, 1.0f},
                                                       {0.5f, 0.02f, 0.02f, 1.0f},
                                                       {0.02f, 0.05f, 0.2f, 1.0f},
                                                       {0.9f, 0.6f, 0.02f, 1.0f}}};
        constexpr Fenrir::Colour LampColour{1.0f, 0.7f, 0.4f, 1.0f};
        constexpr Fenrir::Colour HeadlightColour{1.0f, 0.93f, 0.82f, 1.0f};
        constexpr Fenrir::Colour TaillightColour{1.0f, 0.02f, 0.01f, 1.0f};

        /** @brief The centre line of the `i`th street or avenue, from 0 to Blocks. */
        constexpr float StreetLine(int i)
        {
            return (static_cast<float>(i) - static_cast<float>(Blocks) * 0.5f) * Pitch;
        }

        /** @brief The centre of the `i`th block along an axis, from 0 to Blocks - 1. */
        constexpr float BlockCentre(int i)
        {
            return StreetLine(i) + Pitch * 0.5f;
        }

        /** @brief A colour's RGB times `scale`, alpha 1: what a glowing slab shows for a light of that colour. */
        Fenrir::Colour Scaled(Fenrir::Colour colour, float scale)
        {
            return {colour.r * scale, colour.g * scale, colour.b * scale, 1.0f};
        }

        /** @brief A neon sign: its rect light, and its slab's place and size. */
        struct Sign
        {
            Fenrir::Entity light;
            Mat4 slab;
            Fenrir::Colour colour;
            /** @brief Its intensity as a share of the panel's sign intensity: blade signs are brighter. */
            float share = 1.0f;
        };

        /** @brief A car driving round one lane, wrapping at the city's edge. */
        struct Car
        {
            /** @brief The lane's centre line, as the fixed coordinate across it, and the axis it runs along. */
            float lane = 0.0f;
            bool alongX = true;
            /** @brief +1 or -1 along the axis. */
            float direction = 1.0f;
            /** @brief Where along the axis it is, from -CityHalf to CityHalf. */
            float along = 0.0f;
            float speed = 10.0f;
            Fenrir::Colour paint;
            std::array<Fenrir::Entity, 2> headlights;
        };

        /** @brief One instance list the cars are drawn from, rebuilt each frame they move. */
        struct CarPart
        {
            Fenrir::AssetHandle<Fenrir::InstanceList> list;
            Fenrir::InstanceListData copies;
        };

        /** @brief The city's lights and lists, and what the panel and keys have set, kept as a scene resource. */
        struct CityState
        {
            LightingSettings lighting;
            Fenrir::AssetHandle<Fenrir::Material> street;
            std::vector<Fenrir::Entity> lamps;
            std::vector<Sign> signs;
            Fenrir::AssetHandle<Fenrir::InstanceList> signSlabs;
            std::vector<Car> cars;
            /** @brief The bodies, cabins, headlamps and tail-lights. */
            std::array<CarPart, 4> carParts;
            std::size_t buildings = 0;
            float lampIntensity = 40.0f;
            float signIntensity = 5.0f;
            float headlightIntensity = 60.0f;
            /** @brief The street material's puddles: the share of the street under water. */
            float puddles = 0.5f;
            /** @brief What M brings the puddles back to. */
            float wetPuddles = 0.5f;
            bool moving = true;
            bool clustered = true;
            bool fetched = false;
            /** @brief The frame time, smoothed over about a second, in seconds. */
            float frameTime = 0.0f;
            /** @brief What the lights and slabs were last given, so a change in the panel reaches them once. */
            float appliedLamps = -1.0f;
            float appliedSigns = -1.0f;
            float appliedHeadlights = -1.0f;
            float appliedPuddles = -1.0f;
        };

        Fenrir::AssetHandle<Fenrir::Material> LitMaterial(Fenrir::AssetStore& store, Fenrir::Scene& scene,
                                                          Fenrir::Colour colour, float metallic, float roughness)
        {
            const auto material = store.CreateMaterial(store.Load<Fenrir::Shader>("builtin:lit", scene).handle, scene);
            store.SetColour(material, "baseColour", colour);
            store.SetFloat(material, "metallic", metallic);
            store.SetFloat(material, "roughness", roughness);
            return material;
        }

        /** @brief An entity drawing `copies` of `mesh` in `material`, none of them shadow casters (no light casts). */
        Fenrir::AssetHandle<Fenrir::InstanceList> AddList(Fenrir::AssetStore& store, Fenrir::Scene& scene,
                                                          Fenrir::AssetHandle<Fenrir::Mesh> mesh,
                                                          Fenrir::AssetHandle<Fenrir::Material> material,
                                                          Fenrir::InstanceListData copies)
        {
            const auto handle = store.Add<Fenrir::InstanceList>(std::move(copies), scene);
            Fenrir::Entity entity = scene.GetEntityList().CreateEntity();
            entity.AddComponent<Fenrir::Transform>();
            entity.AddComponent<Fenrir::InstancedMeshRenderer>(
                Fenrir::InstancedMeshRenderer{mesh, material, handle, false});
            return handle;
        }

        /** @brief The street material: the demo's street shader with the fetched asphalt, or a plain dark grey. */
        Fenrir::AssetHandle<Fenrir::Material> StreetMaterial(Fenrir::App& app, Fenrir::Scene& scene, bool& fetched)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            const auto material = EmbeddedMaterial(store, scene, Fenrir::Shaders::LightingDemoStreet);
            if (material.IsEmpty())
            {
                app.Logger()->Error("{}: the street shader did not load", SceneName);
                return material;
            }
            std::array<Fenrir::AssetHandle<Fenrir::Image>, 3> maps;
            for (std::size_t i = 0; i < AsphaltMaps.size() && fetched; ++i)
            {
                const std::optional<std::string> path = FetchedAsset(AsphaltMaps.at(i));
                const auto image = path ? store.Load<Fenrir::Image>(*path, scene) : Fenrir::LoadResult<Fenrir::Image>{};
                fetched = path.has_value() && image.Ok();
                maps.at(i) = image.handle;
            }
            if (!fetched)
            {
                store.SetColour(material, "tint", {0.05f, 0.05f, 0.05f, 1.0f});
                return material;
            }
            // Poly Haven's asphalt is a pale, sunlit grey: darkened to a city street's.
            store.SetColour(material, "tint", {0.55f, 0.55f, 0.55f, 1.0f});
            store.SetTexture(material, "asphaltColour", maps[0]);
            store.SetTexture(material, "asphaltNormal", AsLinear(store, scene, maps[1]));
            store.SetTexture(material, "asphaltArm", AsLinear(store, scene, maps[2]));
            return material;
        }

        /** @brief The ground under the whole city, the sidewalks round each block and the lane markings. */
        void SpawnGround(Fenrir::App& app, Fenrir::Scene& scene, CityState& state,
                         Fenrir::AssetHandle<Fenrir::Mesh> cube, bool& fetched)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            state.street = StreetMaterial(app, scene, fetched);
            Fenrir::Entity ground = scene.GetEntityList().CreateEntity();
            Fenrir::Transform& transform = ground.AddComponent<Fenrir::Transform>();
            transform.pos = {0.0f, -0.1f, 0.0f};
            transform.scale = {2.0f * CityHalf + 40.0f, 0.2f, 2.0f * CityHalf + 40.0f};
            ground.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{cube, state.street, true, false});

            Fenrir::InstanceListData sidewalks;
            Fenrir::InstanceListData markings;
            const float sidewalkSide = BlockSide + 2.0f * Sidewalk;
            for (int x = 0; x < Blocks; ++x)
            {
                for (int z = 0; z < Blocks; ++z)
                {
                    sidewalks.transforms.push_back(Placement({BlockCentre(x), KerbHeight * 0.5f, BlockCentre(z)},
                                                             Quat(1.0f, 0.0f, 0.0f, 0.0f),
                                                             {sidewalkSide, KerbHeight, sidewalkSide}));
                }
            }
            // A dashed centre line down every road between the junctions: 3-metre dashes, 3 metres apart.
            for (int i = 0; i <= Blocks; ++i)
            {
                for (int block = 0; block < Blocks; ++block)
                {
                    for (float t = -sidewalkSide * 0.5f + 1.5f; t < sidewalkSide * 0.5f; t += 6.0f)
                    {
                        const float along = BlockCentre(block) + t;
                        markings.transforms.push_back(Placement({along, 0.005f, StreetLine(i)},
                                                                Quat(1.0f, 0.0f, 0.0f, 0.0f), {3.0f, 0.01f, 0.15f}));
                        markings.transforms.push_back(Placement({StreetLine(i), 0.005f, along},
                                                                Quat(1.0f, 0.0f, 0.0f, 0.0f), {0.15f, 0.01f, 3.0f}));
                    }
                }
            }
            AddList(store, scene, cube, LitMaterial(store, scene, {0.16f, 0.155f, 0.15f, 1.0f}, 0.0f, 0.5f),
                    std::move(sidewalks));
            AddList(store, scene, cube, LitMaterial(store, scene, {0.6f, 0.58f, 0.5f, 1.0f}, 0.0f, 0.45f),
                    std::move(markings));
        }

        /**
         * @brief The buildings, one copy each in the facade shader's list, and the neon signs on the walls that face a
         * street: a shop sign above the ground floor and, on some, a two-sided blade sign standing out from a corner.
         */
        void SpawnBuildings(Fenrir::App& app, Fenrir::Scene& scene, CityState& state,
                            Fenrir::AssetHandle<Fenrir::Mesh> cube, std::mt19937& random)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            const auto facade = EmbeddedMaterial(store, scene, Fenrir::Shaders::LightingDemoFacade);
            if (facade.IsEmpty())
            {
                app.Logger()->Error("{}: the facade shader did not load", SceneName);
                return;
            }
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            const auto pick = [&](std::size_t count) {
                return std::min(static_cast<std::size_t>(unit(random) * static_cast<float>(count)), count - 1);
            };
            Fenrir::InstanceListData buildings;
            Fenrir::EntityList& entities = scene.GetEntityList();
            for (int bx = 0; bx < Blocks; ++bx)
            {
                for (int bz = 0; bz < Blocks; ++bz)
                {
                    for (int lx = 0; lx < LotsPerSide; ++lx)
                    {
                        for (int lz = 0; lz < LotsPerSide; ++lz)
                        {
                            const Vec2 lot{BlockCentre(bx) + (static_cast<float>(lx) - 1.0f) * Lot,
                                           BlockCentre(bz) + (static_cast<float>(lz) - 1.0f) * Lot};
                            // Taller towards the middle of the city.
                            const float centrality = 1.0f - glm::length(lot) / (CityHalf * 1.2f);
                            const float tall = unit(random);
                            const float height = 10.0f + (20.0f + 70.0f * tall * tall) * (0.4f + 0.6f * centrality);
                            const Vec3 size{Lot - 1.0f - 2.0f * unit(random), height, Lot - 1.0f - 2.0f * unit(random)};
                            // Each building fronts the street: pushed to its lot's outer edges, so the walls line up.
                            const Vec2 push{lx == 0 ? -1.0f : (lx == 2 ? 1.0f : 0.0f),
                                            lz == 0 ? -1.0f : (lz == 2 ? 1.0f : 0.0f)};
                            const Vec2 centre = lot + push * (Vec2(Lot - size.x, Lot - size.z) * 0.5f);
                            buildings.transforms.push_back(Placement({centre.x, KerbHeight + height * 0.5f, centre.y},
                                                                     Quat(1.0f, 0.0f, 0.0f, 0.0f), size));
                            Fenrir::Colour light = WindowLight.at(pick(WindowLight.size()));
                            light.a = 0.1f + 0.35f * unit(random);
                            buildings.colours.push_back(light);

                            // A sign on each wall facing a street.
                            for (int axis = 0; axis < 2; ++axis)
                            {
                                const float side = axis == 0 ? push.x : push.y;
                                if (side == 0.0f)
                                {
                                    continue;
                                }
                                const Vec3 out = axis == 0 ? Vec3(side, 0.0f, 0.0f) : Vec3(0.0f, 0.0f, side);
                                const Vec3 along = axis == 0 ? Vec3(0.0f, 0.0f, 1.0f) : Vec3(1.0f, 0.0f, 0.0f);
                                const float halfDepth = (axis == 0 ? size.x : size.z) * 0.5f;
                                const float wallWidth = axis == 0 ? size.z : size.x;
                                const Vec3 wall = Vec3(centre.x, KerbHeight, centre.y) + out * halfDepth;
                                if (unit(random) < 0.8f)
                                {
                                    const float width = std::min(wallWidth * (0.4f + 0.3f * unit(random)), 7.0f);
                                    const Vec3 at = wall + out * 0.25f + Up * 3.7f +
                                                    along * ((unit(random) - 0.5f) * (wallWidth - width) * 0.8f);
                                    const Fenrir::RectLight rect{
                                        Neon.at(pick(Neon.size())), state.signIntensity, width, 0.5f, false, 12.0f};
                                    // A rect light faces its entity's -Z: out from the wall.
                                    const Quat facing = glm::quatLookAt(out, Up);
                                    Fenrir::Entity entity = entities.CreateEntity();
                                    Fenrir::Transform& transform = entity.AddComponent<Fenrir::Transform>();
                                    transform.pos = at;
                                    transform.rot = facing;
                                    entity.AddComponent<Fenrir::RectLight>(rect);
                                    state.signs.push_back(
                                        {entity, Placement(at, facing, {width, 0.5f, 0.05f}), rect.colour, 1.0f});
                                }
                                if (unit(random) < 0.35f && height > 12.0f)
                                {
                                    const float tallness = std::min(4.0f + 5.0f * unit(random), height - 7.0f);
                                    const float corner = unit(random) < 0.5f ? -1.0f : 1.0f;
                                    const Vec3 at = wall + out * 0.75f + Up * (5.5f + tallness * 0.5f) +
                                                    along * (corner * (wallWidth * 0.5f - 1.0f));
                                    const Fenrir::RectLight rect{Neon.at(pick(Neon.size())),
                                                                 state.signIntensity * 1.4f,
                                                                 1.0f,
                                                                 tallness,
                                                                 true,
                                                                 16.0f};
                                    // Standing out from the wall, its faces along the street: its +X points out.
                                    const Quat facing = glm::quatLookAt(along, Up);
                                    Fenrir::Entity entity = entities.CreateEntity();
                                    Fenrir::Transform& transform = entity.AddComponent<Fenrir::Transform>();
                                    transform.pos = at;
                                    transform.rot = facing;
                                    entity.AddComponent<Fenrir::RectLight>(rect);
                                    state.signs.push_back(
                                        {entity, Placement(at, facing, {1.0f, tallness, 0.05f}), rect.colour, 1.4f});
                                }
                            }
                        }
                    }
                }
            }
            state.buildings = buildings.transforms.size();
            AddList(store, scene, cube, facade, std::move(buildings));

            Fenrir::InstanceListData slabs;
            for (const Sign& sign : state.signs)
            {
                slabs.transforms.push_back(sign.slab);
                slabs.colours.push_back(Scaled(sign.colour, state.signIntensity * sign.share));
            }
            const auto unlit = store.CreateMaterial(store.Load<Fenrir::Shader>("builtin:unlit", scene).handle, scene);
            state.signSlabs = AddList(store, scene, cube, unlit, std::move(slabs));
        }

        /** @brief A street lamp on the kerb of every block's side, two a side: a pole, an arm, a glowing head. */
        void SpawnLamps(Fenrir::App& app, Fenrir::Scene& scene, CityState& state,
                        Fenrir::AssetHandle<Fenrir::Mesh> cube)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            constexpr float PoleHeight = 7.5f;
            constexpr float Arm = 1.6f;
            const float kerb = BlockSide * 0.5f + Sidewalk - 0.4f;
            Fenrir::InstanceListData metal;
            Fenrir::InstanceListData heads;
            Fenrir::EntityList& entities = scene.GetEntityList();
            const Quat identity(1.0f, 0.0f, 0.0f, 0.0f);
            for (int bx = 0; bx < Blocks; ++bx)
            {
                for (int bz = 0; bz < Blocks; ++bz)
                {
                    const Vec3 block{BlockCentre(bx), KerbHeight, BlockCentre(bz)};
                    for (const Vec3 out : {Vec3(1.0f, 0.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f),
                                           Vec3(0.0f, 0.0f, -1.0f)})
                    {
                        const Vec3 along{out.z, 0.0f, out.x};
                        for (const float t : {-11.0f, 11.0f})
                        {
                            const Vec3 foot = block + out * kerb + along * t;
                            const Vec3 head = foot + Up * PoleHeight + out * Arm;
                            metal.transforms.push_back(
                                Placement(foot + Up * (PoleHeight * 0.5f), identity, {0.18f, PoleHeight, 0.18f}));
                            const Vec3 armSize = glm::abs(out) * Arm + Vec3(0.12f, 0.12f, 0.12f);
                            metal.transforms.push_back(
                                Placement(foot + Up * PoleHeight + out * (Arm * 0.5f), identity, armSize));
                            const Vec3 headSize =
                                glm::abs(out) * 0.6f + glm::abs(along) * 0.3f + Vec3(0.0f, 0.1f, 0.0f);
                            heads.transforms.push_back(Placement(head - Up * 0.08f, identity, headSize));

                            Fenrir::Entity lamp = entities.CreateEntity();
                            lamp.AddComponent<Fenrir::Transform>().pos = head - Up * 0.3f;
                            lamp.AddComponent<Fenrir::PointLight>(
                                Fenrir::PointLight{LampColour, state.lampIntensity, 18.0f, false});
                            state.lamps.push_back(lamp);
                        }
                    }
                }
            }
            heads.colours.assign(heads.transforms.size(), Scaled(LampColour, 25.0f));
            AddList(store, scene, cube, LitMaterial(store, scene, {0.05f, 0.05f, 0.05f, 1.0f}, 0.8f, 0.45f),
                    std::move(metal));
            const auto unlit = store.CreateMaterial(store.Load<Fenrir::Shader>("builtin:unlit", scene).handle, scene);
            AddList(store, scene, cube, unlit, std::move(heads));
        }

        /** @brief Where `car` is and which way it faces, in the world: its body's origin on the road. */
        Mat4 CarFrame(const Car& car)
        {
            const Vec3 forward = car.alongX ? Vec3(car.direction, 0.0f, 0.0f) : Vec3(0.0f, 0.0f, car.direction);
            const Vec3 at = car.alongX ? Vec3(car.along, 0.0f, car.lane) : Vec3(car.lane, 0.0f, car.along);
            // A car faces its frame's -Z.
            return Placement(at, glm::quatLookAt(forward, Up), Vec3(1.0f));
        }

        /** @brief The cars' copies placed for where they are now, and their headlights moved with them. */
        void PlaceCars(Fenrir::AssetStore& store, CityState& state)
        {
            for (CarPart& part : state.carParts)
            {
                part.copies.transforms.clear();
                part.copies.colours.clear();
            }
            const Quat identity(1.0f, 0.0f, 0.0f, 0.0f);
            const auto add = [&](CarPart& part, const Mat4& frame, Vec3 at, Vec3 size, Fenrir::Colour colour) {
                part.copies.transforms.push_back(frame * Placement(at, identity, size));
                part.copies.colours.push_back(colour);
            };
            // Each headlight looks ahead and a little down, onto the road in front.
            const Quat dipped = Fenrir::Math::AngleAxis(-0.07f, Vec3(1.0f, 0.0f, 0.0f));
            for (Car& car : state.cars)
            {
                const Mat4 frame = CarFrame(car);
                add(state.carParts[0], frame, {0.0f, 0.62f, 0.0f}, {1.85f, 0.72f, 4.5f}, car.paint);
                add(state.carParts[1], frame, {0.0f, 1.22f, 0.3f}, {1.6f, 0.5f, 2.3f}, Fenrir::Colour::White);
                for (const float side : {-1.0f, 1.0f})
                {
                    add(state.carParts[2], frame, {side * 0.62f, 0.72f, -2.26f}, {0.42f, 0.14f, 0.04f},
                        Scaled(HeadlightColour, 30.0f));
                    add(state.carParts[3], frame, {side * 0.7f, 0.78f, 2.26f}, {0.36f, 0.12f, 0.04f},
                        Scaled(TaillightColour, 3.0f));
                }
                const Quat facing = glm::quat_cast(frame) * dipped;
                for (std::size_t i = 0; i < car.headlights.size(); ++i)
                {
                    if (car.headlights.at(i).IsValid())
                    {
                        Fenrir::Transform& transform = car.headlights.at(i).GetComponent<Fenrir::Transform>();
                        const float side = i == 0 ? -1.0f : 1.0f;
                        transform.pos = Vec3(frame * Fenrir::Math::Vec4(side * 0.62f, 0.72f, -2.35f, 1.0f));
                        transform.rot = facing;
                    }
                }
            }
            for (CarPart& part : state.carParts)
            {
                store.Replace(part.list, part.copies);
            }
        }

        /** @brief The traffic: CarsPerLane cars in each of every road's two lanes, each with two spot headlights. */
        void SpawnCars(Fenrir::App& app, Fenrir::Scene& scene, CityState& state, Fenrir::AssetHandle<Fenrir::Mesh> cube,
                       std::mt19937& random)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            Fenrir::EntityList& entities = scene.GetEntityList();
            for (int i = 0; i <= Blocks; ++i)
            {
                for (const bool alongX : {true, false})
                {
                    for (const float direction : {1.0f, -1.0f})
                    {
                        // Driving on the right: facing +X, the right is +Z; facing +Z, it is -X.
                        const float offset = alongX ? direction * LaneOffset : -direction * LaneOffset;
                        const float speed = 8.0f + 5.0f * unit(random);
                        const float start = unit(random) * 2.0f * CityHalf;
                        for (int c = 0; c < CarsPerLane; ++c)
                        {
                            Car car;
                            car.lane = StreetLine(i) + offset;
                            car.alongX = alongX;
                            car.direction = direction;
                            car.along = std::fmod(start + static_cast<float>(c) * 2.0f * CityHalf / CarsPerLane +
                                                      unit(random) * 20.0f,
                                                  2.0f * CityHalf) -
                                        CityHalf;
                            car.speed = speed;
                            car.paint = Paint.at(
                                std::min(static_cast<std::size_t>(unit(random) * Paint.size()), Paint.size() - 1));
                            for (Fenrir::Entity& headlight : car.headlights)
                            {
                                headlight = entities.CreateEntity();
                                headlight.AddComponent<Fenrir::Transform>();
                                headlight.AddComponent<Fenrir::SpotLight>(Fenrir::SpotLight{
                                    HeadlightColour, state.headlightIntensity, 35.0f, 0.15f, 0.45f, false});
                            }
                            state.cars.push_back(car);
                        }
                    }
                }
            }
            const auto unlit = store.Load<Fenrir::Shader>("builtin:unlit", scene).handle;
            const std::array<Fenrir::AssetHandle<Fenrir::Material>, 4> materials{
                LitMaterial(store, scene, Fenrir::Colour::White, 0.6f, 0.3f),
                LitMaterial(store, scene, {0.02f, 0.022f, 0.025f, 1.0f}, 0.0f, 0.08f),
                store.CreateMaterial(unlit, scene), store.CreateMaterial(unlit, scene)};
            for (std::size_t i = 0; i < state.carParts.size(); ++i)
            {
                state.carParts.at(i).list = AddList(store, scene, cube, materials.at(i), {});
            }
            PlaceCars(store, state);
        }

        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            CityState state;
            // A night scene: the lamps' and the signs' spill is dim beside their sources, which AgX lifts at 0 EV.
            state.lighting.exposure = -1.0f;
            state.lighting.bloom.intensity = 0.12f;
            state.lighting.ambientOcclusion.radius = 1.5f;
            // Every light here is small against the dark: the sky is dim, so the neon and the lamps carry the scene.
            state.lighting.skyIntensity = 0.03f;
            bool fetched = true;
            if (const std::optional<std::string> path = FetchedAsset(Panorama))
            {
                if (const auto image = store.Load<Fenrir::Image>(*path, scene); image.Ok())
                {
                    state.lighting.panoramas.push_back({"Kloppenheim 07 (2k)", image.handle});
                }
            }
            if (state.lighting.panoramas.empty())
            {
                fetched = false;
                scene.AddResource<Fenrir::Sky>(Fenrir::ColourSky{{0.004f, 0.005f, 0.009f, 1.0f}});
            }
            // Clustered, whatever another scene left it as.
            Fenrir::Renderer::SetClusteredLights(app, true);

            const auto cube = store.Load<Fenrir::Mesh>("cube.obj#Mesh0", scene);
            if (!cube.Ok())
            {
                app.Logger()->Error("{}: the cube did not load from the asset root", SceneName);
                scene.AddResource<CityState>(std::move(state));
                return;
            }
            std::mt19937 random(2077);
            SpawnGround(app, scene, state, cube.handle, fetched);
            SpawnBuildings(app, scene, state, cube.handle, random);
            SpawnLamps(app, scene, state, cube.handle);
            SpawnCars(app, scene, state, cube.handle, random);
            state.fetched = fetched;
            if (!fetched)
            {
                app.Logger()->Info("{}: the asphalt and the night panorama are fetched assets "
                                   "(FENRIR_FETCH_DEMO_ASSETS); plain dark streets and sky stand in",
                                   SceneName);
            }
            scene.AddResource<CityState>(std::move(state));
        }

        /** @brief Gives the lights, the sign slabs and the street what the panel last set, where it changed. */
        void Apply(Fenrir::AssetStore& store, CityState& state)
        {
            if (state.appliedLamps != state.lampIntensity)
            {
                for (Fenrir::Entity& lamp : state.lamps)
                {
                    lamp.GetComponent<Fenrir::PointLight>().intensity = state.lampIntensity;
                }
                state.appliedLamps = state.lampIntensity;
            }
            if (state.appliedSigns != state.signIntensity)
            {
                Fenrir::InstanceListData slabs;
                for (Sign& sign : state.signs)
                {
                    sign.light.GetComponent<Fenrir::RectLight>().intensity = state.signIntensity * sign.share;
                    slabs.transforms.push_back(sign.slab);
                    slabs.colours.push_back(Scaled(sign.colour, state.signIntensity * sign.share));
                }
                if (!state.signSlabs.IsEmpty())
                {
                    store.Replace(state.signSlabs, std::move(slabs));
                }
                state.appliedSigns = state.signIntensity;
            }
            if (state.appliedHeadlights != state.headlightIntensity)
            {
                for (Car& car : state.cars)
                {
                    for (Fenrir::Entity& headlight : car.headlights)
                    {
                        headlight.GetComponent<Fenrir::SpotLight>().intensity = state.headlightIntensity;
                    }
                }
                state.appliedHeadlights = state.headlightIntensity;
            }
            if (state.appliedPuddles != state.puddles && !state.street.IsEmpty())
            {
                store.SetFloat(state.street, "puddles", state.puddles);
                state.appliedPuddles = state.puddles;
            }
        }

        void Update(Fenrir::App& app, Fenrir::Scene& scene)
        {
            CityState* state = scene.GetResource<CityState>();
            if (state == nullptr)
            {
                return;
            }
            if (!state->fetched)
            {
                ShowMissingAssets(scene, SceneName);
            }
            const Fenrir::Keyboard& keyboard = app.GetResource<Fenrir::Keyboard>();
            if (keyboard.JustPressed(Fenrir::Key::N))
            {
                state->moving = !state->moving;
                app.Logger()->Info("N: the traffic is {}", state->moving ? "moving" : "stopped");
            }
            if (keyboard.JustPressed(Fenrir::Key::M))
            {
                if (state->puddles > 0.0f)
                {
                    state->wetPuddles = state->puddles;
                    state->puddles = 0.0f;
                }
                else
                {
                    state->puddles = state->wetPuddles;
                }
                app.Logger()->Info("M: the streets are {}", state->puddles > 0.0f ? "wet" : "dry");
            }
            bool clustered = state->clustered;
            if (keyboard.JustPressed(Fenrir::Key::C))
            {
                clustered = !clustered;
                app.Logger()->Info("C: the lights are {}", clustered ? "clustered" : "not clustered");
            }

            const Fenrir::RendererStats stats = Fenrir::Renderer::Stats(app);
            const auto delta = static_cast<float>(app.GetTime().deltaTime);
            state->frameTime = state->frameTime == 0.0f ? delta : glm::mix(state->frameTime, delta, 0.05f);
            const std::size_t spots = state->cars.size() * 2;
            const std::size_t lights = state->lamps.size() + state->signs.size() + spots;
            RunLightingPanel(app, scene, state->lighting, [&] {
                // The counts first: the window is long, and they are what a hand check reads.
                ImGui::Text("Frame: %.2f ms (%.0f fps)", state->frameTime * 1000.0f,
                            state->frameTime > 0.0f ? 1.0f / state->frameTime : 0.0f);
                ImGui::Text("Lights: %zu (%zu point lamps, %zu rect signs, %zu spot headlights)", lights,
                            state->lamps.size(), state->signs.size(), spots);
                ImGui::Text("Buildings: %zu  Cars: %zu  Draws: %u  Instances: %u", state->buildings, state->cars.size(),
                            stats.draws, stats.instances);
                ImGui::Checkbox("Clustered (C)", &clustered);
                ImGui::Checkbox("Traffic moving (N)", &state->moving);
                ImGui::SliderFloat("Puddles (M)", &state->puddles, 0.0f, 1.0f);
                ImGui::SliderFloat("Lamp intensity", &state->lampIntensity, 0.0f, 150.0f);
                ImGui::SliderFloat("Sign intensity", &state->signIntensity, 0.0f, 30.0f);
                ImGui::SliderFloat("Headlight intensity", &state->headlightIntensity, 0.0f, 200.0f);
            });
            if (clustered != state->clustered)
            {
                state->clustered = clustered;
                Fenrir::Renderer::SetClusteredLights(app, clustered);
            }

            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            Apply(store, *state);
            if (state->moving && !state->cars.empty())
            {
                for (Car& car : state->cars)
                {
                    car.along += car.direction * car.speed * delta;
                    if (car.along > CityHalf)
                    {
                        car.along -= 2.0f * CityHalf;
                    }
                    else if (car.along < -CityHalf)
                    {
                        car.along += 2.0f * CityHalf;
                    }
                }
                PlaceCars(store, *state);
            }
        }
    } // namespace

    FenrirDemo::DemoScene NeonCity()
    {
        FenrirDemo::DemoScene scene;
        scene.name = SceneName;
        scene.keyHelp = LightingKeyHelp(false) +
                        " N: the traffic stopped or moving. M: the streets dry or wet. C: the lights clustered or not.";
        // The traffic stopped and the streets dried, then through the debug views to the light clusters' heat and back,
        // unclustered and clustered again, wet again and moving again.
        scene.keys = {Fenrir::Key::N, Fenrir::Key::M, Fenrir::Key::G, Fenrir::Key::G, Fenrir::Key::G, Fenrir::Key::G,
                      Fenrir::Key::G, Fenrir::Key::G, Fenrir::Key::C, Fenrir::Key::C, Fenrir::Key::M, Fenrir::Key::N};
        // Above the middle of a street, a few metres up, looking down it between the towers.
        scene.camera.position = {StreetLine(2) - 1.5f, 3.5f, CityHalf - 10.0f};
        scene.camera.yaw = 0.0f;
        scene.camera.pitch = -0.1f;
        scene.spawn = Spawn;
        scene.update = Update;
        return scene;
    }
} // namespace LightingDemo
