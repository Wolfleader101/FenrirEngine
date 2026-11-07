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
#include <utility>
#include <vector>

#include <imgui.h>

#include "DemoMaterials.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/ImageData.hpp"
#include "FenrirAssets/InstanceListData.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirRenderer/Renderer.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirShaders/LightingDemoFacade.hpp"
#include "FenrirShaders/LightingDemoSign.hpp"
#include "FenrirShaders/LightingDemoStreet.hpp"
#include "FenrirWindow/Window.hpp"
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
        /**
         * @brief The facade shader's fetched textures, by its sampler names: Poly Haven's Concrete Tile Facade, Dark
         * Brick Wall and Exterior Wall Cladding, and whether each is linear data (normal and ARM maps) or colour.
         */
        struct FacadeMap
        {
            const char* sampler;
            const char* path;
            bool linear;
        };
        constexpr std::array<FacadeMap, 8> FacadeMaps{{
            {"concreteColour", "concrete_tile_facade/concrete_tile_facade_diff_2k.jpg", false},
            {"concreteNormal", "concrete_tile_facade/concrete_tile_facade_nor_gl_2k.jpg", true},
            {"concreteArm", "concrete_tile_facade/concrete_tile_facade_arm_2k.jpg", true},
            {"brickColour", "dark_brick_wall/dark_brick_wall_diff_2k.jpg", false},
            {"brickNormal", "dark_brick_wall/dark_brick_wall_nor_gl_2k.jpg", true},
            {"brickArm", "dark_brick_wall/dark_brick_wall_arm_2k.jpg", true},
            {"claddingColour", "exterior_wall_cladding/exterior_wall_cladding_diff_2k.jpg", false},
            {"claddingNormal", "exterior_wall_cladding/exterior_wall_cladding_nor_gl_2k.jpg", true},
        }};

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

        /** @brief A colour's RGB times `scale`, alpha 1: what a sign's tubes glow for a light of that colour. */
        Fenrir::Colour Scaled(Fenrir::Colour colour, float scale)
        {
            return {colour.r * scale, colour.g * scale, colour.b * scale, 1.0f};
        }

        /**
         * @brief A night sky made here, linear RGBA32F in the renderer's panorama mapping, to be drawn at the scene's
         * dim sky intensity: a dark blue zenith over the city's own light pollution, a magenta glow rising from the
         * horizon with a warmer band along it, uneven round the compass; a dark ground below.
         */
        Fenrir::ImageData CityGlow(std::uint32_t width)
        {
            constexpr float Pi = std::numbers::pi_v<float>;
            const std::uint32_t height = width / 2;
            std::vector<float> texels(std::size_t{width} * height * 4, 0.0f);
            for (std::uint32_t row = 0; row < height; ++row)
            {
                const float theta = Pi * (static_cast<float>(row) + 0.5f) / static_cast<float>(height);
                for (std::uint32_t column = 0; column < width; ++column)
                {
                    // panorama.frag's inverse: u = 0.5 + atan(x, -z) / 2pi, v = acos(y) / pi.
                    const float phi =
                        2.0f * Pi * ((static_cast<float>(column) + 0.5f) / static_cast<float>(width) - 0.5f);
                    const float up = std::cos(theta);
                    const float uneven = 1.0f + 0.25f * std::sin(phi * 3.0f) + 0.15f * std::sin(phi * 7.0f + 1.3f);
                    // Above the horizon, the glow falls off upwards; below it, the horizon's colour darkens to the
                    // ground, meeting it without a seam.
                    const auto sky = [&](float height) {
                        return Vec3(0.05f, 0.06f, 0.15f) + Vec3(1.6f, 0.45f, 1.1f) * uneven * std::exp(-height * 7.0f) +
                               Vec3(0.9f, 0.35f, 0.1f) * uneven * std::exp(-height * 25.0f);
                    };
                    const Vec3 colour =
                        up >= 0.0f ? sky(up)
                                   : glm::mix(sky(0.0f), Vec3(0.05f, 0.04f, 0.05f), std::min(-up * 8.0f, 1.0f));
                    float* texel = &texels[(std::size_t{row} * width + column) * 4];
                    texel[0] = colour.r;
                    texel[1] = colour.g;
                    texel[2] = colour.b;
                    texel[3] = 1.0f;
                }
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

        /** @brief A neon sign: its rect light, and its board's place and size (the sign shader's copy). */
        struct Sign
        {
            Fenrir::Entity light;
            Mat4 board;
            Fenrir::Colour colour;
            /** @brief Its intensity as a share of the panel's sign intensity: blade signs are brighter. */
            float share = 1.0f;
        };

        /** @brief A car driving round one lane, wrapping at the city's edge. */
        struct Car
        {
            /** @brief Its lane's centre line: the fixed coordinate across the axis it runs along. */
            float across = 0.0f;
            bool alongX = true;
            /** @brief +1 or -1 along the axis. */
            float direction = 1.0f;
            /** @brief Where along the axis it is, from -CityHalf to CityHalf. */
            float along = 0.0f;
            float speed = 10.0f;
            Fenrir::Colour paint;
            std::array<Fenrir::Entity, 2> headlights;
        };

        /** @brief Which of a car's parts a CarPart list draws: its index in CityState::carParts. */
        enum CarPartKind : std::uint8_t
        {
            Body,
            Cabin,
            Headlamp,
            Taillight,
            CarPartKinds
        };

        /** @brief One instance list the cars are drawn from, rebuilt each frame they move. */
        struct CarPart
        {
            Fenrir::AssetHandle<Fenrir::InstanceList> list;
            Fenrir::InstanceListData copies;
        };

        /** @brief A tower's uplight, and its intensity as a multiple of the panel's: taller towers' are brighter. */
        struct WashLight
        {
            Fenrir::Entity light;
            float scale = 1.0f;
        };

        /** @brief The city's lights and lists, and what the panel and keys have set, kept as a scene resource. */
        struct CityState
        {
            LightingSettings lighting;
            Fenrir::AssetHandle<Fenrir::Material> street;
            std::vector<Fenrir::Entity> lamps;
            std::vector<Sign> signs;
            Fenrir::AssetHandle<Fenrir::InstanceList> signBoards;
            std::vector<WashLight> uplights;
            std::vector<Car> cars;
            /** @brief The bodies, cabins, headlamps and tail-lights, by CarPartKind. */
            std::array<CarPart, CarPartKinds> carParts;
            std::size_t buildings = 0;
            /** @brief The neon panels on the walls, which light nothing. */
            std::size_t panels = 0;
            float lampIntensity = 40.0f;
            float signIntensity = 8.0f;
            float headlightIntensity = 60.0f;
            /** @brief A 40-metre uplight's; longer ones are brighter by the square of their reach. */
            float uplightIntensity = 1500.0f;
            /** @brief The street material's puddles: the share of the street under water. */
            float puddles = 0.75f;
            /** @brief What M brings the puddles back to. */
            float wetPuddles = 0.75f;
            bool moving = true;
            bool clustered = true;
            /** @brief Whether presents wait for the display; off, the frame time is the frame's own. */
            bool vsync = true;
            bool appliedVsync = true;
            bool fetched = false;
            /** @brief The frame time, smoothed over about a second, in seconds. */
            float frameTime = 0.0f;
            /** @brief What the lights and boards were last given, so a change in the panel reaches them once. */
            float appliedLamps = -1.0f;
            float appliedSigns = -1.0f;
            float appliedHeadlights = -1.0f;
            float appliedUplights = -1.0f;
            float appliedPuddles = -1.0f;
        };

        /** @brief One of `count` choices, picked evenly by `random`. */
        std::size_t Pick(std::mt19937& random, std::size_t count)
        {
            const float unit = std::uniform_real_distribution<float>(0.0f, 1.0f)(random);
            return std::min(static_cast<std::size_t>(unit * static_cast<float>(count)), count - 1);
        }

        /** @brief The signs' boards, their tubes glowing each sign's colour at the panel's sign intensity times its
         * share. */
        Fenrir::InstanceListData SignBoards(const CityState& state)
        {
            Fenrir::InstanceListData boards;
            for (const Sign& sign : state.signs)
            {
                boards.transforms.push_back(sign.board);
                boards.colours.push_back(Scaled(sign.colour, state.signIntensity * sign.share));
            }
            return boards;
        }

        /**
         * @brief A neon sign: `rect` at `at`, turned by `facing` (a rect light faces its entity's -Z), at `share` of
         * the panel's sign intensity, on a board of its width and height drawn by the sign shader.
         */
        void AddSign(Fenrir::Scene& scene, CityState& state, Vec3 at, Quat facing, Fenrir::RectLight rect, float share)
        {
            rect.intensity = state.signIntensity * share;
            Fenrir::Entity entity = scene.GetEntityList().CreateEntity();
            Fenrir::Transform& transform = entity.AddComponent<Fenrir::Transform>();
            transform.pos = at;
            transform.rot = facing;
            entity.AddComponent<Fenrir::RectLight>(rect);
            state.signs.push_back(
                {entity, Placement(at, facing, {rect.width, rect.height, 0.05f}), rect.colour, share});
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

        /** @brief One box of a building: where its centre is and its size, in metres. */
        struct Tier
        {
            Vec3 centre;
            Vec3 size;
        };

        /** @brief What dresses the buildings beyond their tiers and lit signs, as instance lists. */
        struct Dressing
        {
            /** @brief Plant rooms and boxes on the roofs. */
            Fenrir::InstanceListData plant;
            /** @brief Radio masts. */
            Fenrir::InstanceListData masts;
            /** @brief The masts' red beacons. */
            Fenrir::InstanceListData beacons;
            /** @brief Neon panels stacked up the walls: glowing, but no lights of their own. */
            Fenrir::InstanceListData panels;
        };

        /** @brief The tiers of the building on a lot centred at `lot`, pushed `push` towards its block's edges. */
        std::vector<Tier> BuildingTiers(Vec2 lot, Vec2 push, std::mt19937& random)
        {
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            // Taller and more often a tower towards the middle of the city.
            const float centrality = std::clamp(1.0f - glm::length(lot) / (CityHalf * 1.1f), 0.0f, 1.0f);
            const Vec2 foot{Lot - 1.0f - 1.5f * unit(random), Lot - 1.0f - 1.5f * unit(random)};
            // Its ground tier fronts the street: pushed to its lot's outer edges, so the walls line up.
            const Vec2 centre = lot + push * ((Vec2(Lot) - foot) * 0.5f);
            std::vector<Tier> tiers;
            if (unit(random) < 0.25f + 0.6f * centrality)
            {
                // A tower on a podium, often with a narrower crown.
                const float podium = 8.0f + 10.0f * unit(random);
                tiers.push_back({{centre.x, KerbHeight + podium * 0.5f, centre.y}, {foot.x, podium, foot.y}});
                const float tall = unit(random);
                const float height = (35.0f + 140.0f * tall * tall) * (0.35f + 0.65f * centrality);
                const Vec2 towerFoot = foot * (0.55f + 0.25f * unit(random));
                const float towerBase = KerbHeight + podium;
                tiers.push_back({{centre.x, towerBase + height * 0.5f, centre.y}, {towerFoot.x, height, towerFoot.y}});
                if (unit(random) < 0.55f)
                {
                    const Vec2 crownFoot = towerFoot * (0.6f + 0.2f * unit(random));
                    const float crown = 5.0f + 12.0f * unit(random);
                    tiers.push_back(
                        {{centre.x, towerBase + height + crown * 0.5f, centre.y}, {crownFoot.x, crown, crownFoot.y}});
                }
            }
            else
            {
                const float tall = unit(random);
                const float height = 9.0f + (8.0f + 30.0f * tall * tall) * (0.5f + 0.5f * centrality);
                tiers.push_back({{centre.x, KerbHeight + height * 0.5f, centre.y}, {foot.x, height, foot.y}});
            }
            return tiers;
        }

        /** @brief The top of `tier`, from the ground. */
        float TopOf(const Tier& tier)
        {
            return tier.centre.y + tier.size.y * 0.5f;
        }

        /** @brief One wall of a tier: its foot's centre, the way out of it and along it, and its width. */
        struct Wall
        {
            Vec3 foot;
            Vec3 out;
            Vec3 along;
            float width;
        };

        /** @brief The wall of `tier` facing `out`, one of the four horizontal axes. */
        Wall WallOf(const Tier& tier, Vec3 out)
        {
            const bool alongZ = out.x != 0.0f;
            const float halfDepth = (alongZ ? tier.size.x : tier.size.z) * 0.5f;
            const Vec3 foot{tier.centre.x, tier.centre.y - tier.size.y * 0.5f, tier.centre.z};
            return {foot + out * halfDepth, out, alongZ ? Vec3(0.0f, 0.0f, 1.0f) : Vec3(1.0f, 0.0f, 0.0f),
                    alongZ ? tier.size.z : tier.size.x};
        }

        /**
         * @brief The signs on a street-facing wall of a building's ground tier, `height` tall: a row of shop signs
         * above the ground floor, up to two two-sided blade signs standing out from it and, on some, a column of neon
         * panels flat on the wall.
         */
        void SignWall(Fenrir::Scene& scene, CityState& state, Dressing& dressing, const Wall& wall, float height,
                      std::mt19937& random)
        {
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            const Quat facing = glm::quatLookAt(wall.out, Up);
            // Shop signs, one to every six metres or so of the wall.
            const int shops = std::max(1, static_cast<int>(wall.width / 6.0f));
            const float segment = wall.width / static_cast<float>(shops);
            for (int i = 0; i < shops; ++i)
            {
                if (unit(random) < 0.75f)
                {
                    const float width = segment * (0.5f + 0.35f * unit(random));
                    const float tallness = 0.45f + 0.35f * unit(random);
                    const float offset = (static_cast<float>(i) + 0.5f) * segment - wall.width * 0.5f;
                    AddSign(scene, state, wall.foot + wall.out * 0.25f + Up * 3.8f + wall.along * offset, facing,
                            {Neon.at(Pick(random, Neon.size())), 1.0f, width, tallness, false, 12.0f}, 1.0f);
                }
            }
            // Blade signs standing out from the wall, their faces along the street (their +X points out).
            for (int i = 0; i < 2 && height > 10.0f; ++i)
            {
                if (unit(random) < 0.55f)
                {
                    const float tallness = std::min(3.0f + 6.0f * unit(random), height - 6.0f);
                    const float lowest = 5.0f + tallness * 0.5f;
                    const float y = lowest + unit(random) * std::max(height - 1.0f - tallness * 0.5f - lowest, 0.0f);
                    const float offset = (unit(random) - 0.5f) * (wall.width - 2.0f);
                    AddSign(scene, state, wall.foot + wall.out * 0.75f + Up * y + wall.along * offset,
                            glm::quatLookAt(wall.along, Up),
                            {Neon.at(Pick(random, Neon.size())), 1.0f, 1.0f, tallness, true, 16.0f}, 1.4f);
                }
            }
            // A column of panels flat on the wall, glowing but lighting nothing: colour without the light's cost.
            if (unit(random) < 0.4f && height > 10.0f)
            {
                const float offset = (unit(random) - 0.5f) * (wall.width - 2.0f);
                // Tall boards one above another, mostly in one hue, like a street of vertical signs.
                const Fenrir::Colour hue = Neon.at(Pick(random, Neon.size()));
                for (float y = 6.5f; y < height - 1.5f && y < 22.0f; y += 2.2f)
                {
                    dressing.panels.transforms.push_back(Placement(
                        wall.foot + wall.out * 0.08f + Up * y + wall.along * offset, facing, {0.8f, 1.8f, 0.05f}));
                    const Fenrir::Colour board = unit(random) < 0.25f ? Neon.at(Pick(random, Neon.size())) : hue;
                    dressing.panels.colours.push_back(Scaled(board, 3.0f));
                }
            }
        }

        /** @brief Plant rooms, and on some a radio mast with a beacon, on the roof of `top`. */
        void DressRoof(Dressing& dressing, const Tier& top, std::mt19937& random)
        {
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            const Quat identity(1.0f, 0.0f, 0.0f, 0.0f);
            const float roof = TopOf(top);
            const auto spot = [&](float margin) {
                return Vec2(top.centre.x + (unit(random) - 0.5f) * std::max(top.size.x - margin, 0.0f),
                            top.centre.z + (unit(random) - 0.5f) * std::max(top.size.z - margin, 0.0f));
            };
            const int plant = 1 + static_cast<int>(Pick(random, 3));
            for (int i = 0; i < plant; ++i)
            {
                const Vec3 size{2.0f + 2.0f * unit(random), 1.2f + 1.8f * unit(random), 2.0f + 2.0f * unit(random)};
                const Vec2 at = spot(size.x + 1.0f);
                dressing.plant.transforms.push_back(Placement({at.x, roof + size.y * 0.5f, at.y}, identity, size));
            }
            if (roof > 40.0f && unit(random) < 0.4f)
            {
                const float mast = 6.0f + 10.0f * unit(random);
                const Vec2 at = spot(2.0f);
                dressing.masts.transforms.push_back(
                    Placement({at.x, roof + mast * 0.5f, at.y}, identity, {0.25f, mast, 0.25f}));
                dressing.beacons.transforms.push_back(Placement({at.x, roof + mast, at.y}, identity, Vec3(0.5f)));
            }
        }

        /**
         * @brief Coloured uplights washing a tower's walls: a spot light standing out from each of `walls` at the
         * tower's foot, looking up and a little in, reaching its top.
         */
        void Uplight(Fenrir::Scene& scene, CityState& state, const Tier& tower, std::span<const Vec3> walls,
                     Fenrir::Colour colour)
        {
            // Fading out three-quarters of the way up: a wash, and fewer clusters for each light to reach.
            const float range = tower.size.y * 0.75f + 6.0f;
            // As bright at the top as a short tower's is: the inverse square over the reach.
            const float scale = (range / 40.0f) * (range / 40.0f);
            for (const Vec3 out : walls)
            {
                const Wall wall = WallOf(tower, out);
                Fenrir::Entity entity = scene.GetEntityList().CreateEntity();
                Fenrir::Transform& transform = entity.AddComponent<Fenrir::Transform>();
                transform.pos = wall.foot + out * 2.5f + Up * 0.3f;
                // A spot light shines along its entity's -Z.
                transform.rot = glm::quatLookAt(glm::normalize(Up - out * 0.35f), wall.along);
                entity.AddComponent<Fenrir::SpotLight>(
                    Fenrir::SpotLight{colour, state.uplightIntensity * scale, range, 0.12f, 0.38f, false});
                state.uplights.push_back({entity, scale});
            }
        }

        /**
         * @brief The buildings, each tier one copy in the facade shader's list, with their signs, rooftop billboards,
         * uplights, roof plant and masts.
         */
        void SpawnBuildings(Fenrir::App& app, Fenrir::Scene& scene, CityState& state,
                            Fenrir::AssetHandle<Fenrir::Mesh> cube, Fenrir::AssetHandle<Fenrir::Mesh> sphere,
                            std::mt19937& random, bool& fetched)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            const auto facade = EmbeddedMaterial(store, scene, Fenrir::Shaders::LightingDemoFacade);
            if (facade.IsEmpty())
            {
                app.Logger()->Error("{}: the facade shader did not load", SceneName);
                return;
            }
            // The walls' textures, all or none; without them the walls are a plain dark grey.
            std::array<Fenrir::AssetHandle<Fenrir::Image>, FacadeMaps.size()> maps;
            for (std::size_t i = 0; i < FacadeMaps.size() && fetched; ++i)
            {
                const std::optional<std::string> path = FetchedAsset(FacadeMaps.at(i).path);
                const auto image = path ? store.Load<Fenrir::Image>(*path, scene) : Fenrir::LoadResult<Fenrir::Image>{};
                fetched = path.has_value() && image.Ok();
                maps.at(i) = FacadeMaps.at(i).linear ? AsLinear(store, scene, image.handle) : image.handle;
            }
            if (fetched)
            {
                for (std::size_t i = 0; i < FacadeMaps.size(); ++i)
                {
                    store.SetTexture(facade, FacadeMaps.at(i).sampler, maps.at(i));
                }
            }
            else
            {
                store.SetColour(facade, "wallTint", {0.1f, 0.1f, 0.1f, 1.0f});
            }
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            Fenrir::InstanceListData tiers;
            Dressing dressing;
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
                            const Vec2 push{lx == 0 ? -1.0f : (lx == 2 ? 1.0f : 0.0f),
                                            lz == 0 ? -1.0f : (lz == 2 ? 1.0f : 0.0f)};
                            const std::vector<Tier> building = BuildingTiers(lot, push, random);
                            ++state.buildings;

                            // The facade shader's copy colour: the windows' light, and in alpha the share lit, the
                            // style, and 3 more where the tier has edge strips (facade.frag). A tower's podium keeps
                            // its style but never its strips.
                            const Fenrir::Colour light = WindowLight.at(Pick(random, WindowLight.size()));
                            const float share = 0.1f + 0.35f * unit(random);
                            const auto style = static_cast<float>(Pick(random, 3));
                            const bool strips = unit(random) < (building.size() > 1 ? 0.5f : 0.15f);
                            for (std::size_t i = 0; i < building.size(); ++i)
                            {
                                const bool edged = strips && (building.size() == 1 || i > 0);
                                tiers.transforms.push_back(Placement(
                                    building.at(i).centre, Quat(1.0f, 0.0f, 0.0f, 0.0f), building.at(i).size));
                                tiers.colours.push_back(
                                    {light.r, light.g, light.b, style + (edged ? 3.0f : 0.0f) + share});
                            }

                            // Signs on the ground tier's walls that face a street.
                            const Tier& ground = building.front();
                            std::vector<Vec3> streetWalls;
                            if (push.x != 0.0f)
                            {
                                streetWalls.emplace_back(push.x, 0.0f, 0.0f);
                            }
                            if (push.y != 0.0f)
                            {
                                streetWalls.emplace_back(0.0f, 0.0f, push.y);
                            }
                            for (const Vec3 out : streetWalls)
                            {
                                SignWall(scene, state, dressing, WallOf(ground, out), ground.size.y, random);
                            }

                            // A tower's walls washed by coloured uplights from its podium's roof: those facing the
                            // streets, or two for a tower in a block's middle.
                            if (building.size() > 1)
                            {
                                std::vector<Vec3> washed = streetWalls;
                                if (washed.empty())
                                {
                                    washed = {Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f)};
                                }
                                Uplight(scene, state, building.at(1), washed, Neon.at(Pick(random, Neon.size())));
                            }

                            // A billboard on some low and middling roofs, facing a street.
                            const Tier& top = building.back();
                            if (!streetWalls.empty() && TopOf(top) < 60.0f && unit(random) < 0.3f)
                            {
                                const Wall wall = WallOf(top, streetWalls.front());
                                const float width = std::min(wall.width * 0.8f, 12.0f);
                                const float tallness = 3.0f + 2.0f * unit(random);
                                AddSign(scene, state,
                                        wall.foot - wall.out * 1.0f + Up * (top.size.y + 1.5f + tallness * 0.5f),
                                        glm::quatLookAt(wall.out, Up),
                                        {Neon.at(Pick(random, Neon.size())), 1.0f, width, tallness, false, 25.0f},
                                        1.5f);
                            }
                            DressRoof(dressing, top, random);
                        }
                    }
                }
            }
            AddList(store, scene, cube, facade, std::move(tiers));
            // The signs' boards and the wall boards share the sign shader: neon tubes on a dark board.
            auto sign = EmbeddedMaterial(store, scene, Fenrir::Shaders::LightingDemoSign);
            if (sign.IsEmpty())
            {
                app.Logger()->Error("{}: the sign shader did not load", SceneName);
                sign = UnlitMaterial(store, scene);
            }
            state.signBoards = AddList(store, scene, cube, sign, SignBoards(state));
            state.panels = dressing.panels.transforms.size();
            AddList(store, scene, cube, sign, std::move(dressing.panels));
            AddList(store, scene, cube, LitMaterial(store, scene, {0.12f, 0.12f, 0.12f, 1.0f}, 0.0f, 0.7f),
                    std::move(dressing.plant));
            AddList(store, scene, cube, LitMaterial(store, scene, {0.05f, 0.05f, 0.05f, 1.0f}, 0.8f, 0.45f),
                    std::move(dressing.masts));
            if (sphere.IsEmpty())
            {
                return;
            }
            dressing.beacons.colours.assign(dressing.beacons.transforms.size(), Scaled(TaillightColour, 8.0f));
            AddList(store, scene, sphere, UnlitMaterial(store, scene), std::move(dressing.beacons));
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
            AddList(store, scene, cube, UnlitMaterial(store, scene), std::move(heads));
        }

        /** @brief Where `car` is and which way it faces, in the world: its body's origin on the road. */
        Mat4 CarFrame(const Car& car)
        {
            const Vec3 forward = car.alongX ? Vec3(car.direction, 0.0f, 0.0f) : Vec3(0.0f, 0.0f, car.direction);
            const Vec3 at = car.alongX ? Vec3(car.along, 0.0f, car.across) : Vec3(car.across, 0.0f, car.along);
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
                add(state.carParts[Body], frame, {0.0f, 0.62f, 0.0f}, {1.85f, 0.72f, 4.5f}, car.paint);
                add(state.carParts[Cabin], frame, {0.0f, 1.22f, 0.3f}, {1.6f, 0.5f, 2.3f}, Fenrir::Colour::White);
                for (const float side : {-1.0f, 1.0f})
                {
                    add(state.carParts[Headlamp], frame, {side * 0.62f, 0.72f, -2.26f}, {0.42f, 0.14f, 0.04f},
                        Scaled(HeadlightColour, 30.0f));
                    add(state.carParts[Taillight], frame, {side * 0.7f, 0.78f, 2.26f}, {0.36f, 0.12f, 0.04f},
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
                            car.across = StreetLine(i) + offset;
                            car.alongX = alongX;
                            car.direction = direction;
                            car.along = std::fmod(start + static_cast<float>(c) * 2.0f * CityHalf / CarsPerLane +
                                                      unit(random) * 20.0f,
                                                  2.0f * CityHalf) -
                                        CityHalf;
                            car.speed = speed;
                            car.paint = Paint.at(Pick(random, Paint.size()));
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
            // By CarPartKind: painted bodies, dark glass cabins, glowing headlamps and tail-lights.
            const std::array<Fenrir::AssetHandle<Fenrir::Material>, CarPartKinds> materials{
                LitMaterial(store, scene, Fenrir::Colour::White, 0.6f, 0.3f),
                LitMaterial(store, scene, {0.02f, 0.022f, 0.025f, 1.0f}, 0.0f, 0.08f), UnlitMaterial(store, scene),
                UnlitMaterial(store, scene)};
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
            // PBR Neutral keeps the neon saturated where AgX turns it pastel; T still steps to AgX. A strong, wide
            // bloom spreads each sign's colour round it, and -0.5 EV keeps the dim spill on the upper walls at night.
            state.lighting.tonemapper = Fenrir::Tonemapper::PbrNeutral;
            state.lighting.exposure = -0.5f;
            state.lighting.bloom = {0.3f, 0.85f};
            state.lighting.ambientOcclusion.radius = 1.5f;
            // The grade of a cyberpunk night: teal shadows and magenta highlights, split a little under mid grey so the
            // magenta reaches the lit walls, with more contrast for the streets, and a slightly cool white balance.
            state.lighting.colourGrading.temperature = 6000.0f;
            state.lighting.colourGrading.contrast = 1.15f;
            state.lighting.colourGrading.shadowTint = Fenrir::Colour{0.25f, 0.6f, 0.65f, 1.0f};
            state.lighting.colourGrading.highlightTint = Fenrir::Colour{0.85f, 0.4f, 0.75f, 1.0f};
            state.lighting.colourGrading.balance = 0.15f;
            // The wet streets mirror the glowing strips, boards and windows on screen. The signs' boards are drawn,
            // so their rect lights keep visibleSurface, and their glints yield to the boards' reflections.
            state.lighting.reflections.enabled = true;
            // Every light here is small against the dark: the sky is dim, so the neon and the lamps carry the scene.
            state.lighting.skyIntensity = 0.03f;
            // The city's own glow first; Poly Haven's overcast night (S) when fetched.
            state.lighting.panoramas.push_back(
                {"City glow (generated)", store.Add<Fenrir::Image>(CityGlow(1024), scene)});
            bool fetched = false;
            if (const std::optional<std::string> path = FetchedAsset(Panorama))
            {
                if (const auto image = store.Load<Fenrir::Image>(*path, scene); image.Ok())
                {
                    state.lighting.panoramas.push_back({"Kloppenheim 07 (2k)", image.handle});
                    fetched = true;
                }
            }
            // Clustered and with vsync, whatever another scene left them as.
            Fenrir::Renderer::SetClusteredLights(app, true);
            Fenrir::Window::SetSwapInterval(app, Fenrir::Window::MainWindow(), 1);

            const auto cube = store.Load<Fenrir::Mesh>("cube.obj#Mesh0", scene);
            if (!cube.Ok())
            {
                app.Logger()->Error("{}: the cube did not load from the asset root", SceneName);
                scene.AddResource<CityState>(std::move(state));
                return;
            }
            // Only the masts' beacons are round: without the sphere they are left out.
            const auto sphere = store.Load<Fenrir::Mesh>("sphere.obj#Mesh0", scene);
            std::mt19937 random(2077);
            SpawnGround(app, scene, state, cube.handle, fetched);
            SpawnBuildings(app, scene, state, cube.handle, sphere.handle, random, fetched);
            SpawnLamps(app, scene, state, cube.handle);
            SpawnCars(app, scene, state, cube.handle, random);
            state.fetched = fetched;
            if (!fetched)
            {
                app.Logger()->Info("{}: the asphalt, the walls' textures and the night panorama are fetched assets "
                                   "(FENRIR_FETCH_DEMO_ASSETS); plain dark streets and walls under the generated sky "
                                   "stand in",
                                   SceneName);
            }
            scene.AddResource<CityState>(std::move(state));
        }

        /** @brief Gives the lights, the sign boards and the street what the panel last set, where it changed. */
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
                for (Sign& sign : state.signs)
                {
                    sign.light.GetComponent<Fenrir::RectLight>().intensity = state.signIntensity * sign.share;
                }
                if (!state.signBoards.IsEmpty())
                {
                    store.Replace(state.signBoards, SignBoards(state));
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
            if (state.appliedUplights != state.uplightIntensity)
            {
                for (WashLight& uplight : state.uplights)
                {
                    uplight.light.GetComponent<Fenrir::SpotLight>().intensity = state.uplightIntensity * uplight.scale;
                }
                state.appliedUplights = state.uplightIntensity;
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
            if (keyboard.JustPressed(Fenrir::Key::R))
            {
                Fenrir::Reflections& reflections = state->lighting.reflections;
                reflections.enabled = !reflections.enabled;
                app.Logger()->Info("R: reflections are {}", reflections.enabled ? "on" : "off");
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
            const std::size_t headlights = state->cars.size() * 2;
            const std::size_t lights = state->lamps.size() + state->signs.size() + headlights + state->uplights.size();
            RunLightingPanel(app, scene, state->lighting, [&] {
                // The counts first: the window is long, and they are what a hand check reads.
                ImGui::Text("Frame: %.2f ms (%.0f fps)", state->frameTime * 1000.0f,
                            state->frameTime > 0.0f ? 1.0f / state->frameTime : 0.0f);
                ImGui::Text("Lights: %zu: %zu rect signs, %zu point lamps,", lights, state->signs.size(),
                            state->lamps.size());
                ImGui::Text("  %zu spot headlights, %zu spot uplights", headlights, state->uplights.size());
                ImGui::Text("Buildings: %zu  Neon panels: %zu  Cars: %zu", state->buildings, state->panels,
                            state->cars.size());
                ImGui::Text("Draws: %u  Instances: %u", stats.draws, stats.instances);
                ImGui::Checkbox("Clustered (C)", &clustered);
                ImGui::SameLine();
                ImGui::Checkbox("Vsync", &state->vsync);
                ImGui::Checkbox("Traffic moving (N)", &state->moving);
                ImGui::SliderFloat("Puddles (M)", &state->puddles, 0.0f, 1.0f);
                ImGui::Checkbox("Reflections (R)", &state->lighting.reflections.enabled);
                ImGui::SliderFloat("Lamp intensity", &state->lampIntensity, 0.0f, 150.0f);
                ImGui::SliderFloat("Sign intensity", &state->signIntensity, 0.0f, 30.0f);
                ImGui::SliderFloat("Headlight intensity", &state->headlightIntensity, 0.0f, 200.0f);
                ImGui::SliderFloat("Uplight intensity", &state->uplightIntensity, 0.0f, 5000.0f);
            });
            if (state->vsync != state->appliedVsync)
            {
                // The main window's swap interval: the renderer presents to it. False on the null backend's no window.
                Fenrir::Window::SetSwapInterval(app, Fenrir::Window::MainWindow(), state->vsync ? 1 : 0);
                state->appliedVsync = state->vsync;
            }
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
        scene.keyHelp = LightingKeyHelp(true) +
                        " N: the traffic stopped or moving. M: the streets dry or wet. R: reflections off or on. C: the "
                        "lights clustered or not.";
        // The traffic stopped and the streets dried, then through the debug views to the reflections' and back,
        // unclustered and clustered again, wet again, reflections off and on again, moving again, and the other sky
        // where there is one.
        scene.keys = {Fenrir::Key::N, Fenrir::Key::M, Fenrir::Key::G, Fenrir::Key::G, Fenrir::Key::G,
                      Fenrir::Key::G, Fenrir::Key::G, Fenrir::Key::G, Fenrir::Key::G, Fenrir::Key::G,
                      Fenrir::Key::C, Fenrir::Key::C, Fenrir::Key::M, Fenrir::Key::R, Fenrir::Key::R,
                      Fenrir::Key::N, Fenrir::Key::S};
        // Above the middle of a street, a few metres up, looking down it between the towers.
        scene.camera.position = {StreetLine(2) - 1.5f, 3.5f, CityHalf - 10.0f};
        scene.camera.yaw = 0.0f;
        scene.camera.pitch = -0.1f;
        scene.spawn = Spawn;
        scene.update = Update;
        return scene;
    }
} // namespace LightingDemo
