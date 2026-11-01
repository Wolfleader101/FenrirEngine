#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/ImageData.hpp"
#include "FenrirAssets/InstanceListData.hpp"
#include "FenrirAssets/MaterialData.hpp"
#include "FenrirAssets/MeshData.hpp"
#include "FenrirAssets/Model.hpp"
#include "FenrirAssets/ShaderData.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirRenderer/Renderer.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirShaders/LightingDemoGrass.hpp"
#include "FenrirShaders/LightingDemoGround.hpp"
#include "FetchedAssets.hpp"
#include "ForestGround.hpp"
#include "LightingPanel.hpp"
#include "LightingScenes.hpp"
#include "MeshTools.hpp"

namespace LightingDemo
{
    namespace
    {
        using Fenrir::Math::Vec2;
        using Fenrir::Math::Vec3;

        constexpr const char* SceneName = "Forest clearing";
        constexpr float Pi = std::numbers::pi_v<float>;
        const Vec3 Up{0.0f, 1.0f, 0.0f};

        /** @brief How many grass blades grow in the clearing. */
        constexpr std::size_t GrassBlades = 200000;

        /**
         * @brief The way the sunlight travels: from Qwantani Late Afternoon's sun (its brightest texel), 19 degrees up
         * ahead and to the right of where the camera starts, so the clearing is lit from behind its far trees. The
         * demo's own sky puts its sun here too.
         */
        const Vec3 SunTravel = glm::normalize(Vec3(-0.557f, -0.327f, 0.763f));
        const Fenrir::Colour SunColour{1.0f, 0.8f, 0.58f, 1.0f};
        constexpr float SunIntensity = 5.0f;
        /** @brief The tall trees leave a gap this many radians either side of the sun, so it lights the clearing. */
        constexpr float SunGap = 0.3f;

        /** @brief The fraction of the forest's copies drawn, which N steps through. */
        constexpr std::array<int, 3> DensitySteps{25, 50, 100};

        constexpr const char* Panorama = "panoramas/qwantani_late_afternoon_puresky_2k.hdr";
        /**
         * @brief Each ground layer's base colour, GL normal map and ARM map: soil, litter, path. The soil has no ARM
         * map (ground.frag's eight textures).
         */
        constexpr std::array<std::array<const char*, 3>, 3> GroundSets{{
            {"forest_ground_04/forest_ground_04_diff_2k.jpg", "forest_ground_04/forest_ground_04_nor_gl_2k.jpg",
             nullptr},
            {"forest_leaves_04/forest_leaves_04_diff_2k.jpg", "forest_leaves_04/forest_leaves_04_nor_gl_2k.jpg",
             "forest_leaves_04/forest_leaves_04_arm_2k.jpg"},
            {"stony_dirt_path/stony_dirt_path_diff_2k.jpg", "stony_dirt_path/stony_dirt_path_nor_gl_2k.jpg",
             "stony_dirt_path/stony_dirt_path_arm_2k.jpg"},
        }};
        /** @brief The ground shader's names for its layers, as GroundSets orders them. */
        constexpr std::array<const char*, 3> GroundLayers{"soil", "litter", "path"};

        /** @brief Where on the ground a species grows, by how far past the clearing's edge (Forest::PastEdge). */
        enum class Habitat : std::uint8_t
        {
            /** @brief Among the trees, out to Forest::ForestReach. */
            Forest,
            /** @brief Along the clearing's edge, a few metres either side of it. */
            Edge,
            /** @brief Anywhere from the clearing's middle to the forest's reach. */
            Everywhere,
        };

        /**
         * @brief A tree's level of detail, for trees standing up to `pastEdge` metres past the clearing's edge: one
         * card in `keepEvery` of its foliage kept and grown by `grow` (ThinnedCards), its trunk and branches simplified
         * on `trunkCells` cells a side or whole at 0 (clustering smears their bark across its UV seams, which shows
         * near), whether it casts the sun's shadow, and how dark it is drawn. The nearest trees are a few metres from
         * the camera, the farthest about ninety. Every cascade draws every caster (there is no culling yet), so the
         * deep forest casts nothing and is darkened instead, for the shade of the trees in front of it.
         */
        struct TreeDetail
        {
            float pastEdge;
            int keepEvery;
            float grow;
            int trunkCells;
            bool castShadows;
            float tone;
        };
        // Each kept card grows by a little less than the square root of how many it stands for, so a crown keeps most
        // of its cover: the fir's twigs are 800k sprig cards of four triangles each.
        constexpr std::array<TreeDetail, 3> TreeDetails{{
            {7.0f, 16, 3.4f, 0, true, 1.0f},
            {16.0f, 48, 5.8f, 32, true, 0.7f},
            {std::numeric_limits<float>::max(), 128, 9.0f, 20, false, 0.4f},
        }};

        /**
         * @brief One fetched Poly Haven species (ADR-0052) and how it grows: each of its model's nodes that draws is a
         * kind, with one instance list per level of detail; each of a kind's meshes one InstancedMeshRenderer.
         * Its foliage material is drawn Masked, cut out by its alpha map: Poly Haven's JPEG glTFs leave the alpha out
         * of the base colour, so their leaf and twig cards draw solid whether the material is MASK (the ferns and
         * shrubs), BLEND (the trees) or OPAQUE (the saplings); the demo fetches each one's alpha map and puts it back
         * (MergeAlpha). It then cuts out in the depth prepass and the shadow cascades too.
         */
        struct Species
        {
            const char* model;
            int count;
            float minScale = 1.0f;
            float maxScale = 1.0f;
            Habitat habitat = Habitat::Forest;
            /** @brief Its copies stand this far apart at least, in metres; 0 for no limit. */
            float spacing = 0.0f;
            /**
             * @brief How far past the clearing's edge it grows, and casts the sun's shadow, in metres. Every cascade
             * draws every caster, so the understory deep in the trees, which nobody sees, is left out, and only what
             * stands near the clearing casts.
             */
            float reach = std::numeric_limits<float>::max();
            float shadowReach = std::numeric_limits<float>::max();
            /** @brief Cells a side its meshes are simplified on (solid rocks); 0 keeps them whole. */
            int simplifyCells = 0;
            bool castShadows = true;
            /** @brief Turned every way (pebbles), else only about +Y. */
            bool tumbled = false;
            /** @brief Thinned with distance by TreeDetails, and kept out of the gap towards the sun. */
            bool tree = false;
            /** @brief Its copies are darkened as the forest deepens, for the canopy's shade. */
            bool understory = true;
            /** @brief Its leaf or twig material, by index in its model, and that material's alpha map; -1 for none. */
            int foliage = -1;
            const char* alpha = nullptr;
        };
        // About 20k copies, half of them pebbles. The trees are Poly Haven's Fir Tree 01 (three trees of 11 to 17
        // metres, 0.4 to 4 million twig triangles each), Tree Small 02, a broadleaf grown to 8 to 11 metres, and Fir
        // Sapling, young firs of 2 to 4 metres filling below the crowns, all thinned with distance. Rocks are
        // simplified; ferns and shrubs whole.
        const std::array<Species, 13> ForestSpecies{{
            {.model = "fir_tree_01/fir_tree_01_1k.gltf",
             .count = 900,
             .minScale = 0.85f,
             .maxScale = 1.15f,
             .spacing = 3.0f,
             .tree = true,
             .understory = false,
             .foliage = 2,
             .alpha = "fir_tree_01/textures/fir_tree_01_twig_alpha_1k.jpg"},
            {.model = "tree_small_02/tree_small_02_1k.gltf",
             .count = 110,
             .minScale = 2.2f,
             .maxScale = 3.0f,
             .spacing = 4.5f,
             .tree = true,
             .understory = false,
             .foliage = 1,
             .alpha = "tree_small_02/textures/tree_small_02_leaves_alpha_1k.jpg"},
            {.model = "fir_sapling/fir_sapling_1k.gltf",
             .count = 160,
             .minScale = 1.8f,
             .maxScale = 3.5f,
             .spacing = 2.5f,
             .tree = true,
             .understory = false,
             .foliage = 1,
             .alpha = "fir_sapling/textures/fir_sapling_twigs_alpha_1k.jpg"},
            {.model = "fern_02/fern_02_1k.gltf",
             .count = 5000,
             .minScale = 0.8f,
             .maxScale = 1.6f,
             .habitat = Habitat::Everywhere,
             .reach = 14.0f,
             .shadowReach = 3.0f,
             .foliage = 0,
             .alpha = "fern_02/textures/fern_02_alpha_1k.jpg"},
            {.model = "shrub_03/shrub_03_1k.gltf",
             .count = 2000,
             .minScale = 1.5f,
             .maxScale = 3.0f,
             .habitat = Habitat::Edge,
             .shadowReach = 3.0f,
             .foliage = 0,
             .alpha = "shrub_03/textures/shrub_03_alpha_1k.jpg"},
            {.model = "shrub_04/shrub_04_1k.gltf",
             .count = 150,
             .minScale = 1.0f,
             .maxScale = 2.0f,
             .habitat = Habitat::Edge,
             .shadowReach = 3.0f,
             .foliage = 0,
             .alpha = "shrub_04/textures/shrub_04_alpha_1k.jpg"},
            {.model = "rock_moss_set_01/rock_moss_set_01_1k.gltf",
             .count = 60,
             .minScale = 0.3f,
             .maxScale = 0.9f,
             .habitat = Habitat::Everywhere,
             .reach = 25.0f,
             .simplifyCells = 20},
            {.model = "rock_moss_set_02/rock_moss_set_02_1k.gltf",
             .count = 120,
             .minScale = 0.3f,
             .maxScale = 0.8f,
             .habitat = Habitat::Everywhere,
             .reach = 25.0f,
             .simplifyCells = 16},
            {.model = "rock_09/rock_09_1k.gltf",
             .count = 10000,
             .minScale = 1.5f,
             .maxScale = 5.0f,
             .habitat = Habitat::Everywhere,
             .reach = 12.0f,
             .simplifyCells = 8,
             .castShadows = false,
             .tumbled = true},
            {.model = "dead_tree_trunk/dead_tree_trunk_1k.gltf",
             .count = 10,
             .minScale = 0.9f,
             .maxScale = 1.4f,
             .spacing = 5.0f},
            {.model = "tree_stump_01/tree_stump_01_1k.gltf",
             .count = 24,
             .minScale = 0.6f,
             .maxScale = 1.2f,
             .spacing = 2.0f},
            {.model = "root_cluster_01/root_cluster_01_1k.gltf",
             .count = 8,
             .minScale = 0.6f,
             .maxScale = 1.0f,
             .habitat = Habitat::Edge,
             .spacing = 6.0f},
            {.model = "dry_branches_medium_01/dry_branches_medium_01_1k.gltf",
             .count = 1200,
             .minScale = 0.8f,
             .maxScale = 1.5f,
             .reach = 20.0f,
             .shadowReach = 6.0f},
        }};

        /**
         * @brief A model placed by hand: the foreground's mossy boulder and its neighbour, and the fallen log beside
         * the path. `node` is the model's node; it stands at `at`, turned by `yaw`, sunk `sink` metres, and the grass
         * and plants keep `clear` metres from it (the log's row of circles is HeroClearings').
         */
        struct Hero
        {
            const char* model;
            std::size_t node;
            Vec2 at;
            float yaw;
            float scale;
            float sink;
            float clear;
        };
        const std::array<Hero, 3> Heroes{{
            {"rock_moss_set_01/rock_moss_set_01_1k.gltf", 0, {1.8f, 12.2f}, 0.9f, 1.6f, 0.35f, 3.0f},
            {"rock_moss_set_01/rock_moss_set_01_1k.gltf", 3, {4.3f, 14.3f}, 2.1f, 0.8f, 0.2f, 1.4f},
            {"dead_tree_trunk_02/dead_tree_trunk_02_1k.gltf", 0, {-6.2f, 6.0f}, 0.35f, 1.3f, 0.1f, 0.0f},
        }};
        /** @brief The fallen log's length: its model's 4.1 metres at its scale. */
        constexpr float LogLength = 4.1f * 1.3f;

        /** @brief One instance list the scene draws: its full copies, and the handle its renderers draw. */
        struct CopyList
        {
            Fenrir::InstanceListData copies;
            Fenrir::AssetHandle<Fenrir::InstanceList> handle;
        };

        /** @brief The forest's lists and what the panel and keys have set, kept as a scene resource. */
        struct ForestState
        {
            LightingSettings lighting;
            std::vector<CopyList> lists;
            /** @brief The grass's entities, whose MeshRenderers M hides and shows. */
            std::vector<Fenrir::Entity> grass;
            std::size_t blades = 0;
            std::size_t copies = 0;
            std::size_t trees = 0;
            int density = 100;
            /** @brief The density the lists were last cut to. */
            int listedDensity = 100;
            bool fetched = false;
            bool grassVisible = true;
            /** @brief The frame time, smoothed over about a second, in seconds. */
            float frameTime = 0.0f;
        };

        /** @brief A material drawing with the embedded shader container `bytes`; empty if it does not load. */
        Fenrir::AssetHandle<Fenrir::Material> EmbeddedMaterial(Fenrir::AssetStore& store, Fenrir::Scene& scene,
                                                               std::span<const std::uint8_t> bytes)
        {
            Fenrir::ShaderData shader;
            const auto raw = std::as_bytes(bytes);
            shader.bytes.assign(raw.begin(), raw.end());
            return store.CreateMaterial(store.Add<Fenrir::Shader>(std::move(shader), scene), scene);
        }

        /** @brief A builtin:lit material of `colour` and `roughness`. */
        Fenrir::AssetHandle<Fenrir::Material> LitMaterial(Fenrir::AssetStore& store, Fenrir::Scene& scene,
                                                          Fenrir::Colour colour, float roughness)
        {
            const auto material = store.CreateMaterial(store.Load<Fenrir::Shader>("builtin:lit", scene).handle, scene);
            store.SetColour(material, "baseColour", colour);
            store.SetFloat(material, "roughness", roughness);
            return material;
        }

        /**
         * @brief `image` again, read as linear: JPEGs load as sRGB, which a normal or ARM map is not. Empty if `image`
         * does not resolve.
         */
        Fenrir::AssetHandle<Fenrir::Image> AsLinear(Fenrir::AssetStore& store, Fenrir::Scene& scene,
                                                    Fenrir::AssetHandle<Fenrir::Image> image)
        {
            const Fenrir::ImageData* data = store.TryGet(image);
            if (data == nullptr)
            {
                return {};
            }
            Fenrir::ImageData linear = *data;
            linear.colourSpace = Fenrir::ColourSpace::Linear;
            return store.Add<Fenrir::Image>(std::move(linear), scene);
        }

        /** @brief The forest floor's material, and whether its fetched textures loaded. */
        struct GroundMaterial
        {
            Fenrir::AssetHandle<Fenrir::Material> material;
            bool fetched = false;
        };

        /**
         * @brief The forest floor: the demo's ground shader with Poly Haven's Forest Ground 04 (soil), Forest Leaves 04
         * (litter) and Stony Dirt Path (path) when they were fetched, else plain earth colours for its three layers.
         */
        GroundMaterial MakeGroundMaterial(Fenrir::App& app, Fenrir::Scene& scene)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            GroundMaterial ground{EmbeddedMaterial(store, scene, Fenrir::Shaders::LightingDemoGround)};
            if (ground.material.IsEmpty())
            {
                app.Logger()->Error("{}: the ground shader did not load", SceneName);
                return ground;
            }
            std::array<std::array<Fenrir::AssetHandle<Fenrir::Image>, 3>, 3> images;
            ground.fetched = true;
            for (std::size_t layer = 0; layer < GroundSets.size() && ground.fetched; ++layer)
            {
                for (std::size_t map = 0; map < 3 && ground.fetched; ++map)
                {
                    if (GroundSets.at(layer).at(map) == nullptr)
                    {
                        continue;
                    }
                    const std::optional<std::string> path = FetchedAsset(GroundSets.at(layer).at(map));
                    const auto image =
                        path ? store.Load<Fenrir::Image>(*path, scene) : Fenrir::LoadResult<Fenrir::Image>{};
                    ground.fetched = path.has_value() && image.Ok();
                    images.at(layer).at(map) = image.handle;
                }
            }
            if (!ground.fetched)
            {
                store.SetColour(ground.material, "soilTint", {0.2f, 0.16f, 0.09f, 1.0f});
                store.SetColour(ground.material, "litterTint", {0.11f, 0.08f, 0.05f, 1.0f});
                store.SetColour(ground.material, "pathTint", {0.3f, 0.25f, 0.17f, 1.0f});
                return ground;
            }
            // The fetched path and soil are pale under a low sun beside the dark litter: toned down to sit with it.
            store.SetColour(ground.material, "soilTint", {0.7f, 0.68f, 0.62f, 1.0f});
            store.SetColour(ground.material, "pathTint", {0.62f, 0.58f, 0.52f, 1.0f});
            for (std::size_t layer = 0; layer < GroundSets.size(); ++layer)
            {
                const std::string name = GroundLayers.at(layer);
                store.SetTexture(ground.material, name + "Colour", images.at(layer)[0]);
                store.SetTexture(ground.material, name + "Normal", AsLinear(store, scene, images.at(layer)[1]));
                if (GroundSets.at(layer)[2] != nullptr)
                {
                    store.SetTexture(ground.material, name + "Arm", AsLinear(store, scene, images.at(layer)[2]));
                }
            }
            return ground;
        }

        /**
         * @brief A late-afternoon sky made here, linear RGBA32F in the renderer's panorama mapping: blue overhead
         * paling to a warm haze at the horizon, brightest towards the sun, with the sun's disc and glow where the
         * DirectionalLight comes from, and a dark ground below. The scene's sky without the fetched panorama.
         */
        Fenrir::ImageData AfternoonSky(std::uint32_t width)
        {
            const std::uint32_t height = width / 2;
            std::vector<float> texels(std::size_t{width} * height * 4, 0.0f);
            const Vec3 toSun = -SunTravel;
            for (std::uint32_t row = 0; row < height; ++row)
            {
                const float theta = Pi * (static_cast<float>(row) + 0.5f) / static_cast<float>(height);
                for (std::uint32_t column = 0; column < width; ++column)
                {
                    // panorama.frag's inverse: u = 0.5 + atan(x, -z) / 2pi, v = acos(y) / pi.
                    const float u = (static_cast<float>(column) + 0.5f) / static_cast<float>(width);
                    const float phi = 2.0f * Pi * (u - 0.5f);
                    const Vec3 direction{std::sin(theta) * std::sin(phi), std::cos(theta),
                                         -std::sin(theta) * std::cos(phi)};
                    const float sun = std::max(glm::dot(direction, toSun), 0.0f);
                    Vec3 colour;
                    if (direction.y >= 0.0f)
                    {
                        const float up = std::pow(direction.y, 0.5f);
                        colour = glm::mix(Vec3(1.3f, 1.0f, 0.75f), Vec3(0.25f, 0.42f, 0.8f), up);
                        colour += Vec3(1.5f, 1.0f, 0.55f) * std::pow(sun, 8.0f);
                        colour += Vec3(60.0f, 48.0f, 34.0f) * std::pow(sun, 2000.0f);
                    }
                    else
                    {
                        colour = glm::mix(Vec3(0.12f, 0.1f, 0.07f), Vec3(0.03f, 0.03f, 0.02f),
                                          std::min(-direction.y * 4.0f, 1.0f));
                    }
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

        /** @brief Whether `ground` lies in the bearing the tall trees leave open towards the sun. */
        bool InSunGap(Vec2 ground)
        {
            const Vec2 toSun = glm::normalize(Vec2(-SunTravel.x, -SunTravel.z));
            return std::acos(std::clamp(glm::dot(glm::normalize(ground), toSun), -1.0f, 1.0f)) < SunGap;
        }

        /** @brief Where a copy of `species` stands, or none to skip this try. */
        std::optional<Vec2> GroundSpot(std::mt19937& random, const Species& species)
        {
            const Habitat habitat = species.habitat;
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            const float angle = 2.0f * Pi * unit(random);
            // Evenly over the disc out to the forest's reach, then kept by habitat.
            const Vec2 ground = Vec2(std::cos(angle), std::sin(angle)) * Forest::ForestReach * std::sqrt(unit(random));
            const float past = Forest::PastEdge(ground);
            const bool fits = habitat == Habitat::Forest ? past > 1.0f
                              : habitat == Habitat::Edge ? past > -3.0f && past < 6.0f
                                                         : true;
            if (!fits || past > species.reach || Forest::PathWeight(ground) > 0.05f ||
                (species.tree && InSunGap(ground)))
            {
                return std::nullopt;
            }
            return ground;
        }

        /** @brief One copy of a species: where it stands, its transform (without its node's own) and its tint. */
        struct Copy
        {
            Vec2 ground;
            Fenrir::Math::Mat4 transform;
            Fenrir::Colour colour;
        };

        /**
         * @brief Every copy of `species`, each placed in its habitat and out of `clear`, turned and scaled at random
         * and tinted.
         */
        std::vector<Copy> Plant(const Species& species, std::span<const Forest::Clear> clear, std::uint32_t seed)
        {
            std::mt19937 random(seed);
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            std::uniform_real_distribution<float> symmetric(-1.0f, 1.0f);
            std::vector<Copy> copies;
            // Rejection stops after twelve tries a copy in all, so a crowded species simply grows fewer.
            for (int tries = 0; static_cast<int>(copies.size()) < species.count && tries < species.count * 12; ++tries)
            {
                const std::optional<Vec2> spot = GroundSpot(random, species);
                if (!spot || std::ranges::any_of(clear, [&](const Forest::Clear& circle) {
                        return glm::distance(*spot, circle.centre) < circle.radius;
                    }))
                {
                    continue;
                }
                if (species.spacing > 0.0f && std::ranges::any_of(copies, [&](const Copy& other) {
                        return glm::distance(other.ground, *spot) < species.spacing;
                    }))
                {
                    continue;
                }
                // Small more often than large.
                const float scale =
                    species.minScale + (species.maxScale - species.minScale) * std::pow(unit(random), 2.0f);
                Fenrir::Math::Quat rotation = Fenrir::Math::AngleAxis(2.0f * Pi * unit(random), Up);
                if (species.tumbled)
                {
                    Vec3 axis{symmetric(random), symmetric(random), symmetric(random)};
                    axis = glm::length(axis) > 0.01f ? glm::normalize(axis) : Up;
                    rotation = Fenrir::Math::AngleAxis(2.0f * Pi * unit(random), axis);
                }
                // Sunk a little, so tumbled and leaning copies do not float on the slope.
                const Vec3 position = Forest::OnGround(*spot) - Up * (species.tumbled ? 0.02f * scale : 0.05f);
                const float light = 0.85f + 0.15f * unit(random);
                // The canopy's shade on what grows under it: the sky's light, which GTAO's short reach misses.
                const float shade =
                    species.understory ? 1.0f - 0.45f * std::clamp(Forest::PastEdge(*spot) / 15.0f, 0.0f, 1.0f) : 1.0f;
                const float tone = light * shade;
                copies.push_back({*spot,
                                  Placement(position, rotation, Vec3(scale)),
                                  {tone, tone, tone * (0.92f + 0.08f * unit(random)), 1.0f}});
            }
            return copies;
        }

        /** @brief One mesh a kind draws, with its material, and whether it is its species' foliage. */
        struct Part
        {
            Fenrir::AssetHandle<Fenrir::Mesh> mesh;
            Fenrir::AssetHandle<Fenrir::Material> material;
            bool foliage = false;
        };

        /**
         * @brief A kind of a species, one model node that draws: its parts, and its own turn and scale (its position
         * is dropped).
         */
        struct Kind
        {
            std::vector<Part> parts;
            Fenrir::Math::Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
            Vec3 scale{1.0f};

            [[nodiscard]] Fenrir::Math::Mat4 Rest() const
            {
                return Placement(Vec3(0.0f), rotation, scale);
            }
        };

        /** @brief The foliage's alpha cutoff, and how much its alpha map is strengthened (MergeAlpha). */
        constexpr float FoliageCutoff = 0.3f;
        constexpr float FoliageAlphaGain = 1.6f;

        /**
         * @brief Gives `material` the alpha of the fetched map `alphaPath` (its red channel) in a copy of its base
         * colour texture, and makes it Masked. The alpha is strengthened by FoliageAlphaGain and cut at FoliageCutoff,
         * because thin needles and leaves average towards transparent in the texture's smaller mips and would vanish
         * with distance at the usual 0.5. False, changing nothing, if either image is missing or not RGBA8.
         */
        bool MergeAlpha(Fenrir::AssetStore& store, Fenrir::Scene& scene, Fenrir::AssetHandle<Fenrir::Material> material,
                        const char* alphaPath)
        {
            const std::optional<std::string> path = FetchedAsset(alphaPath);
            const auto alphaImage =
                path ? store.Load<Fenrir::Image>(*path, scene) : Fenrir::LoadResult<Fenrir::Image>{};
            const std::optional<Fenrir::MaterialTexture> base = store.TryGetTexture(material, "baseColourTexture");
            const Fenrir::ImageData* colour = base ? store.TryGet(base->image) : nullptr;
            const Fenrir::ImageData* alpha = path && alphaImage.Ok() ? store.TryGet(alphaImage.handle) : nullptr;
            if (colour == nullptr || alpha == nullptr || colour->format != Fenrir::ImageFormat::RGBA8 ||
                alpha->format != Fenrir::ImageFormat::RGBA8 || alpha->width == 0 || alpha->height == 0)
            {
                return false;
            }
            Fenrir::ImageData cut = *colour;
            for (std::uint32_t y = 0; y < cut.height; ++y)
            {
                // The nearest texel of the alpha map, which is usually the same size.
                const std::uint32_t ay = y * alpha->height / cut.height;
                for (std::uint32_t x = 0; x < cut.width; ++x)
                {
                    const std::uint32_t ax = x * alpha->width / cut.width;
                    const auto value = static_cast<float>(alpha->pixels[(std::size_t{ay} * alpha->width + ax) * 4]);
                    cut.pixels[(std::size_t{y} * cut.width + x) * 4 + 3] =
                        static_cast<std::byte>(std::min(value * FoliageAlphaGain, 255.0f));
                }
            }
            store.SetTexture(material, "baseColourTexture", store.Add<Fenrir::Image>(std::move(cut), scene),
                             base->sampler);
            store.SetBlendMode(material, Fenrir::BlendMode::Masked);
            store.SetAlphaCutoff(material, FoliageCutoff);
            return true;
        }

        /**
         * @brief The model at `path`'s drawing nodes, each a kind. Its material number `foliage` takes the alpha map
         * `alpha` and is drawn Masked (MergeAlpha). Empty if the model or the alpha map did not load.
         */
        std::vector<Kind> LoadKinds(Fenrir::AssetStore& store, Fenrir::Scene& scene, const std::string& path,
                                    int foliage = -1, const char* alpha = nullptr)
        {
            const auto loaded = store.Load<Fenrir::Model>(path, scene);
            const Fenrir::Model* model = loaded.Ok() ? store.TryGet(loaded.handle) : nullptr;
            std::vector<Kind> kinds;
            if (model == nullptr)
            {
                return kinds;
            }
            const auto foliageMaterial = foliage >= 0 && static_cast<std::size_t>(foliage) < model->materials.size()
                                             ? model->materials[static_cast<std::size_t>(foliage)]
                                             : Fenrir::AssetHandle<Fenrir::Material>{};
            if (!foliageMaterial.IsEmpty() && alpha != nullptr && !MergeAlpha(store, scene, foliageMaterial, alpha))
            {
                return kinds;
            }
            for (const Fenrir::ModelNode& node : model->nodes)
            {
                if (node.meshes.empty())
                {
                    continue;
                }
                Kind& kind = kinds.emplace_back();
                for (const Fenrir::ModelMesh& mesh : node.meshes)
                {
                    kind.parts.push_back({mesh.mesh, mesh.material, mesh.material == foliageMaterial});
                }
                // The node's position only lays the file's kinds out side by side.
                kind.rotation = node.rest.rot;
                kind.scale = node.rest.scale;
            }
            return kinds;
        }

        /** @brief Adds `copies` as a list drawn by one InstancedMeshRenderer per part, children of `forest`. */
        void AddList(Fenrir::AssetStore& store, Fenrir::Scene& scene, Fenrir::Entity forest, ForestState& state,
                     Fenrir::InstanceListData copies, std::span<const Part> parts, bool castShadows)
        {
            if (copies.transforms.empty())
            {
                return;
            }
            state.copies += copies.transforms.size();
            const auto handle = store.Add<Fenrir::InstanceList>(copies, scene);
            Fenrir::EntityList& entities = scene.GetEntityList();
            for (const Part& part : parts)
            {
                Fenrir::Entity entity = entities.CreateEntity();
                entity.AddComponent<Fenrir::Transform>();
                entity.AddComponent<Fenrir::InstancedMeshRenderer>(
                    Fenrir::InstancedMeshRenderer{part.mesh, part.material, handle, castShadows});
                forest.AddChild(entity);
            }
            state.lists.push_back({std::move(copies), handle});
        }

        /**
         * @brief `part` at a species' level of detail `level`: a tree's foliage thinned and its wood simplified by
         * TreeDetails, a rock simplified, anything else whole (its two levels differ only in casting).
         */
        Part AtDetail(Fenrir::AssetStore& store, Fenrir::Scene& scene, const Species& species, const Part& part,
                      std::size_t level)
        {
            const Fenrir::MeshData* full = store.TryGet(part.mesh);
            Part made = part;
            if (full == nullptr)
            {
                return made;
            }
            if (species.tree)
            {
                const TreeDetail& detail = TreeDetails.at(level);
                if (part.foliage)
                {
                    made.mesh = store.Add<Fenrir::Mesh>(ThinnedCards(*full, detail.keepEvery, detail.grow), scene);
                }
                else if (detail.trunkCells > 0)
                {
                    made.mesh = store.Add<Fenrir::Mesh>(Simplified(*full, detail.trunkCells), scene);
                }
            }
            else if (species.simplifyCells > 0)
            {
                made.mesh = store.Add<Fenrir::Mesh>(Simplified(*full, species.simplifyCells), scene);
            }
            return made;
        }

        /**
         * @brief Plants `species` under `forest`: its copies split round-robin between its kinds and, for a tree, by
         * TreeDetails, one list each. False if the model did not load.
         */
        bool SpawnSpecies(Fenrir::AssetStore& store, Fenrir::Scene& scene, Fenrir::Entity forest, ForestState& state,
                          const Species& species, const std::string& path, std::span<const Forest::Clear> clear,
                          std::uint32_t seed)
        {
            const std::vector<Kind> kinds = LoadKinds(store, scene, path, species.foliage, species.alpha);
            if (kinds.empty())
            {
                return false;
            }
            // A tree's levels are TreeDetails'; anything else's are two, casting within its shadow reach and not past
            // it.
            const std::size_t levels = species.tree ? TreeDetails.size() : 2;
            std::vector<std::vector<Fenrir::InstanceListData>> lists(kinds.size(),
                                                                     std::vector<Fenrir::InstanceListData>(levels));
            const std::vector<Copy> copies = Plant(species, clear, seed);
            for (std::size_t i = 0; i < copies.size(); ++i)
            {
                const std::size_t v = i % kinds.size();
                std::size_t level = 0;
                const float past = Forest::PastEdge(copies[i].ground);
                if (!species.tree)
                {
                    level = past > species.shadowReach ? 1 : 0;
                }
                while (species.tree && level + 1 < levels && past > TreeDetails.at(level).pastEdge)
                {
                    ++level;
                }
                lists[v][level].transforms.push_back(copies[i].transform * kinds[v].Rest());
                Fenrir::Colour colour = copies[i].colour;
                if (species.tree)
                {
                    const float tone = TreeDetails.at(level).tone;
                    colour = {colour.r * tone, colour.g * tone, colour.b * tone, 1.0f};
                }
                lists[v][level].colours.push_back(colour);
            }
            if (species.tree)
            {
                state.trees += copies.size();
            }
            for (std::size_t v = 0; v < kinds.size(); ++v)
            {
                for (std::size_t level = 0; level < levels; ++level)
                {
                    if (lists[v][level].transforms.empty())
                    {
                        continue;
                    }
                    std::vector<Part> parts;
                    for (const Part& part : kinds[v].parts)
                    {
                        parts.push_back(AtDetail(store, scene, species, part, level));
                    }
                    const bool casts =
                        species.castShadows && (species.tree ? TreeDetails.at(level).castShadows : level == 0);
                    AddList(store, scene, forest, state, std::move(lists[v][level]), parts, casts);
                }
            }
            return true;
        }

        /**
         * @brief Where `hero` stands, as an entity's transform, for a model node turned by `restRotation` and scaled by
         * `restScale`.
         */
        Fenrir::Transform HeroTransform(const Hero& hero, Fenrir::Math::Quat restRotation, Vec3 restScale)
        {
            return {Forest::OnGround(hero.at) - Up * hero.sink, Fenrir::Math::AngleAxis(hero.yaw, Up) * restRotation,
                    restScale * hero.scale};
        }

        /**
         * @brief The foreground's boulders and the log beside the path, each a MeshRenderer per mesh of its node, set
         * into the ground. False if a model did not load.
         */
        bool SpawnHeroes(Fenrir::AssetStore& store, Fenrir::Scene& scene)
        {
            Fenrir::EntityList& entities = scene.GetEntityList();
            for (const Hero& hero : Heroes)
            {
                const std::optional<std::string> path = FetchedAsset(hero.model);
                const std::vector<Kind> kinds = path ? LoadKinds(store, scene, *path) : std::vector<Kind>{};
                if (hero.node >= kinds.size())
                {
                    return false;
                }
                const Kind& kind = kinds[hero.node];
                for (const Part& part : kind.parts)
                {
                    Fenrir::Entity entity = entities.CreateEntity();
                    entity.AddComponent<Fenrir::Transform>(HeroTransform(hero, kind.rotation, kind.scale));
                    entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{part.mesh, part.material});
                }
            }
            return true;
        }

        /**
         * @brief Stand-ins for the fetched models, from the committed cube and sphere: tall dark trunks in the forest
         * and grey boulders over the ground, two instance lists, and the foreground boulders and the log in their
         * places, so the scene still casts shadows on the grass and the path.
         */
        void SpawnStandIns(Fenrir::AssetStore& store, Fenrir::Scene& scene, Fenrir::Entity forest, ForestState& state,
                           std::span<const Forest::Clear> clear)
        {
            const auto cube = store.Load<Fenrir::Mesh>("cube.obj#Mesh0", scene);
            const auto sphere = store.Load<Fenrir::Mesh>("sphere.obj#Mesh0", scene);
            if (!cube.Ok() || !sphere.Ok())
            {
                return;
            }
            const Part rock{sphere.handle, LitMaterial(store, scene, {0.35f, 0.36f, 0.3f, 1.0f}, 0.9f)};
            const Part bark{cube.handle, LitMaterial(store, scene, {0.18f, 0.12f, 0.08f, 1.0f}, 0.9f)};
            // Both meshes are a unit across round their middle: a trunk is 0.6 by 14 metres, standing on the ground.
            const Fenrir::Math::Mat4 trunk =
                Placement(Vec3(0.0f, 7.0f, 0.0f), {1.0f, 0.0f, 0.0f, 0.0f}, Vec3(0.6f, 14.0f, 0.6f));
            const Species trees{.model = "", .count = 150, .spacing = 4.2f, .tree = true, .understory = false};
            const Species boulders{
                .model = "", .count = 300, .minScale = 0.3f, .maxScale = 1.5f, .habitat = Habitat::Everywhere};
            Fenrir::InstanceListData trunks;
            for (const Copy& copy : Plant(trees, clear, 101))
            {
                trunks.transforms.push_back(copy.transform * trunk);
                trunks.colours.push_back(copy.colour);
            }
            state.trees = trunks.transforms.size();
            AddList(store, scene, forest, state, std::move(trunks), std::span(&bark, 1), true);
            Fenrir::InstanceListData rocks;
            for (const Copy& copy : Plant(boulders, clear, 102))
            {
                rocks.transforms.push_back(copy.transform);
                rocks.colours.push_back(copy.colour);
            }
            AddList(store, scene, forest, state, std::move(rocks), std::span(&rock, 1), true);
            // The heroes' places: a big boulder, a smaller one, and a log lying along its yaw.
            Fenrir::EntityList& entities = scene.GetEntityList();
            const std::array<std::pair<Part, Vec3>, 3> shapes{
                {{rock, {4.0f, 2.4f, 5.0f}}, {rock, {1.8f, 1.4f, 1.8f}}, {bark, {LogLength, 0.9f, 0.9f}}}};
            for (std::size_t i = 0; i < Heroes.size(); ++i)
            {
                const Hero hero = Heroes.at(i);
                Fenrir::Entity entity = entities.CreateEntity();
                Fenrir::Transform transform = HeroTransform(hero, {1.0f, 0.0f, 0.0f, 0.0f}, Vec3(1.0f));
                transform.scale = shapes.at(i).second;
                entity.AddComponent<Fenrir::Transform>(transform);
                entity.AddComponent<Fenrir::MeshRenderer>(
                    Fenrir::MeshRenderer{shapes.at(i).first.mesh, shapes.at(i).first.material});
            }
        }

        /**
         * @brief The grass: Forest::GrassChunks' meshes, one MeshRenderer each, through the demo's wind shader. Logs an
         * error and spawns nothing if the shader cannot be made.
         */
        void SpawnGrass(Fenrir::App& app, Fenrir::Scene& scene, ForestState& state,
                        std::span<const Forest::Clear> clear)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            const auto material = EmbeddedMaterial(store, scene, Fenrir::Shaders::LightingDemoGrass);
            if (material.IsEmpty())
            {
                app.Logger()->Error("{}: the grass shader did not load", SceneName);
                return;
            }
            Fenrir::EntityList& entities = scene.GetEntityList();
            for (Fenrir::MeshData& chunk : Forest::GrassChunks(GrassBlades, clear, 17))
            {
                // Two faces of seven vertices a blade.
                state.blades += chunk.positions.size() / 14;
                Fenrir::Entity entity = entities.CreateEntity();
                entity.AddComponent<Fenrir::Transform>();
                entity.AddComponent<Fenrir::MeshRenderer>(
                    Fenrir::MeshRenderer{store.Add<Fenrir::Mesh>(std::move(chunk), scene), material});
                state.grass.push_back(entity);
            }
        }

        /** @brief The circles the grass and the plants keep out of: under the boulders and along the log. */
        std::vector<Forest::Clear> HeroClearings()
        {
            std::vector<Forest::Clear> clear;
            for (const Hero& hero : Heroes)
            {
                if (hero.clear > 0.0f)
                {
                    clear.push_back({hero.at, hero.clear});
                }
            }
            // The log, as a row of circles along its length: its model lies along X, turned by its yaw about +Y.
            const Hero& log = Heroes.back();
            const Vec2 along{std::cos(log.yaw), -std::sin(log.yaw)};
            for (float t = -0.5f; t <= 0.5f; t += 0.125f)
            {
                clear.push_back({log.at + along * (t * LogLength), 0.7f});
            }
            return clear;
        }

        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            ForestState state;
            // Shadows across the clearing and into the trees round it, whose 17-metre crowns throw them far.
            state.lighting.sunShadows.shadowDistance = 70.0f;
            state.lighting.ambientOcclusion.radius = 1.0f;
            state.lighting.bloom.intensity = 0.06f;
            state.lighting.exposure = -0.5f;
            if (const std::optional<std::string> path = FetchedAsset(Panorama))
            {
                if (const auto image = store.Load<Fenrir::Image>(*path, scene); image.Ok())
                {
                    state.lighting.panoramas.push_back({"Qwantani Late Afternoon (2k)", image.handle});
                }
            }
            bool fetched = !state.lighting.panoramas.empty();
            state.lighting.panoramas.push_back(
                {"Procedural late afternoon", store.Add<Fenrir::Image>(AfternoonSky(1024), scene)});

            Fenrir::EntityList& entities = scene.GetEntityList();
            Fenrir::Entity sun = entities.CreateEntity();
            sun.AddComponent<Fenrir::Transform>().rot = glm::quatLookAt(SunTravel, Up);
            sun.AddComponent<Fenrir::DirectionalLight>(Fenrir::DirectionalLight{SunColour, SunIntensity});

            const GroundMaterial ground = MakeGroundMaterial(app, scene);
            fetched = fetched && ground.fetched;
            Fenrir::Entity floor = entities.CreateEntity();
            floor.AddComponent<Fenrir::Transform>();
            floor.AddComponent<Fenrir::MeshRenderer>(
                Fenrir::MeshRenderer{store.Add<Fenrir::Mesh>(Forest::GroundMesh(), scene), ground.material});

            const std::vector<Forest::Clear> clear = HeroClearings();
            SpawnGrass(app, scene, state, clear);

            // Every species or none: configure fetches every file of the manifest or fails.
            Fenrir::Entity forest = entities.CreateEntity();
            forest.AddComponent<Fenrir::Transform>();
            std::vector<std::string> paths;
            for (const Species& species : ForestSpecies)
            {
                if (const std::optional<std::string> path = FetchedAsset(species.model))
                {
                    paths.push_back(*path);
                }
            }
            fetched = fetched && paths.size() == ForestSpecies.size() && SpawnHeroes(store, scene);
            if (fetched)
            {
                for (std::size_t i = 0; i < ForestSpecies.size(); ++i)
                {
                    if (!SpawnSpecies(store, scene, forest, state, ForestSpecies.at(i), paths.at(i), clear,
                                      static_cast<std::uint32_t>(41 + i)))
                    {
                        app.Logger()->Error("{}: {} did not load from the cache", SceneName, ForestSpecies.at(i).model);
                    }
                }
            }
            else
            {
                SpawnStandIns(store, scene, forest, state, clear);
                app.Logger()->Info("{}: the trees, plants, rocks, ground textures and panorama are fetched assets "
                                   "(FENRIR_FETCH_DEMO_ASSETS); the grass grows, and trunks and boulders stand in",
                                   SceneName);
            }
            state.fetched = fetched;
            scene.AddResource<ForestState>(std::move(state));
        }

        void Update(Fenrir::App& app, Fenrir::Scene& scene)
        {
            ForestState* state = scene.GetResource<ForestState>();
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
                const auto next = std::ranges::find_if(DensitySteps, [&](int step) { return step > state->density; });
                state->density = next == DensitySteps.end() ? DensitySteps.front() : *next;
                app.Logger()->Info("N: {}% of the forest", state->density);
            }
            if (keyboard.JustPressed(Fenrir::Key::M))
            {
                state->grassVisible = !state->grassVisible;
            }

            const Fenrir::RendererStats stats = Fenrir::Renderer::Stats(app);
            const auto delta = static_cast<float>(app.GetTime().deltaTime);
            state->frameTime = state->frameTime == 0.0f ? delta : glm::mix(state->frameTime, delta, 0.05f);
            RunLightingPanel(app, scene, state->lighting, [&] {
                ImGui::SliderInt("Forest density % (N)", &state->density, 5, 100);
                ImGui::Checkbox("Grass (M)", &state->grassVisible);
                ImGui::Text("Forest: %zu copies in %zu lists, %zu of them trees", state->copies, state->lists.size(),
                            state->trees);
                ImGui::Text("Grass: %zu blades in %zu chunks", state->blades, state->grass.size());
                ImGui::Text("Draws: %u  Instances: %u  Shadow draws: %u", stats.draws, stats.instances,
                            stats.shadowDraws);
                ImGui::Text("Frame: %.2f ms (%.0f fps)", state->frameTime * 1000.0f,
                            state->frameTime > 0.0f ? 1.0f / state->frameTime : 0.0f);
            });

            // A new density cuts each list again: Replace bumps its revision, so the renderer uploads it once more.
            if (state->density != state->listedDensity)
            {
                Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
                for (const CopyList& list : state->lists)
                {
                    const std::size_t total = list.copies.transforms.size();
                    const std::size_t share = (total * static_cast<std::size_t>(state->density) + 99) / 100;
                    store.Replace(list.handle, FirstCopies(list.copies, share));
                }
                state->listedDensity = state->density;
            }
            for (const Fenrir::Entity& chunk : state->grass)
            {
                chunk.GetComponent<Fenrir::MeshRenderer>().visible = state->grassVisible;
            }
        }
    } // namespace

    FenrirDemo::DemoScene ForestClearing()
    {
        FenrirDemo::DemoScene scene;
        scene.name = SceneName;
        scene.keyHelp = LightingKeyHelp(true) + " N: 25%, 50% or all of the forest. M: the grass hidden or shown.";
        // Thin the forest and back, the grass off and on, through the debug views with the shadows off and on, and the
        // other sky.
        scene.keys = {Fenrir::Key::N, Fenrir::Key::M, Fenrir::Key::G, Fenrir::Key::G, Fenrir::Key::M, Fenrir::Key::G,
                      Fenrir::Key::H, Fenrir::Key::G, Fenrir::Key::H, Fenrir::Key::S, Fenrir::Key::N, Fenrir::Key::N};
        // On the path at the clearing's south edge, looking north along it towards the low sun beyond the trees.
        scene.camera.position = Forest::OnGround({-3.2f, 17.5f}) + Vec3(0.0f, 1.7f, 0.0f);
        scene.camera.yaw = -0.3f;
        scene.camera.pitch = 0.04f;
        scene.spawn = Spawn;
        scene.update = Update;
        return scene;
    }
} // namespace LightingDemo
