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

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/ImageData.hpp"
#include "FenrirAssets/InstanceListData.hpp"
#include "FenrirAssets/MeshData.hpp"
#include "FenrirAssets/Model.hpp"
#include "FenrirAssets/ShaderData.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirRenderer/Renderer.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirShaders/LightingDemoGrass.hpp"
#include "FetchedAssets.hpp"
#include "LightingPanel.hpp"
#include "LightingScenes.hpp"
#include "SimplifiedMesh.hpp"

namespace LightingDemo
{
    namespace
    {
        using Fenrir::Math::Vec2;
        using Fenrir::Math::Vec3;

        constexpr const char* SceneName = "Forest clearing";
        constexpr float Pi = std::numbers::pi_v<float>;

        /** @brief The open grass in the middle, and the forest round it out to its edge, in metres from the centre. */
        constexpr float ClearingRadius = 13.0f;
        constexpr float ForestOuter = 55.0f;
        /** @brief The ground's half size: past the forest's edge, so the panorama meets it beyond the trees. */
        constexpr float GroundHalf = 90.0f;
        /** @brief How many metres one repeat of the ground's textures covers. */
        constexpr float GroundTile = 3.0f;

        /** @brief The grass: square patches of one generated mesh, on a grid out to this radius. */
        constexpr float GrassRadius = 23.0f;
        constexpr float PatchSize = 4.0f;
        constexpr int BladesPerPatch = 1800;

        /**
         * @brief The way the sunlight travels: from the bearing of Autumn Forest 04's sun (its brightest texel), so the
         * shadows fall the way the panorama's light does, but 15 degrees up rather than its 8, where the ground's
         * N.L of 0.14 left the shadows hard to see. The demo's own golden-hour sky puts its sun here.
         */
        const Vec3 SunTravel = glm::normalize(Vec3(-0.570f, -0.259f, 0.780f));
        const Fenrir::Colour SunColour{1.0f, 0.62f, 0.36f, 1.0f};
        constexpr float SunIntensity = 4.0f;
        /** @brief The trees leave a gap this many radians either side of the sun, so it reaches the clearing. */
        constexpr float SunGap = 0.4f;

        /** @brief The fraction of the forest's copies drawn, which N steps through. */
        constexpr std::array<int, 3> DensitySteps{25, 50, 100};

        /** @brief Where a species grows. */
        enum class Ground : std::uint8_t
        {
            /** @brief In the forest round the clearing, leaving the gap towards the sun. */
            Forest,
            /** @brief Anywhere from the clearing's middle to the forest's edge, thinning into the clearing. */
            Anywhere,
        };

        /**
         * @brief One fetched Poly Haven species (ADR-0052): its glTF, how many copies of it grow, their sizes and
         * where, and whether its meshes are simplified (solid rocks only) and cast shadows. Each of the model's nodes
         * is a variant with one instance list; each of a variant's meshes one InstancedMeshRenderer drawing it.
         */
        struct Species
        {
            const char* model;
            int count;
            float minScale;
            float maxScale;
            Ground ground;
            /** @brief Cells a side its meshes are simplified on; 0 keeps them whole. */
            int simplifyCells;
            bool castShadows;
            /** @brief Turned every way (pebbles), else only about +Y. */
            bool tumbled;
            /** @brief How close two copies may stand, in metres; 0 for no limit. */
            float spacing;
        };
        // About 20k copies, budgeted by triangles: the saplings' twigs have 120k to 160k each, so there are few trees
        // and the panorama's autumn forest stands behind them. Rocks are simplified; leaves, twigs and branches cannot
        // be (vertex clustering would melt the leaves' cards).
        constexpr std::array<Species, 10> ForestSpecies{{
            {"fir_sapling/fir_sapling_1k.gltf", 45, 3.5f, 6.0f, Ground::Forest, 0, true, false, 4.0f},
            {"pine_sapling_small/pine_sapling_small_1k.gltf", 40, 3.5f, 6.0f, Ground::Forest, 0, true, false, 4.0f},
            {"fern_02/fern_02_1k.gltf", 5000, 0.8f, 1.6f, Ground::Anywhere, 0, true, false, 0.0f},
            {"shrub_03/shrub_03_1k.gltf", 3000, 1.5f, 3.0f, Ground::Anywhere, 0, true, false, 0.0f},
            {"shrub_04/shrub_04_1k.gltf", 250, 1.0f, 2.0f, Ground::Forest, 0, true, false, 0.0f},
            {"rock_07/rock_07_1k.gltf", 900, 1.5f, 5.0f, Ground::Anywhere, 24, true, false, 0.0f},
            {"rock_09/rock_09_1k.gltf", 8500, 1.5f, 5.0f, Ground::Anywhere, 8, false, true, 0.0f},
            {"dead_tree_trunk/dead_tree_trunk_1k.gltf", 18, 0.9f, 1.4f, Ground::Anywhere, 0, true, false, 5.0f},
            {"tree_stump_01/tree_stump_01_1k.gltf", 40, 0.6f, 1.2f, Ground::Forest, 0, true, false, 2.0f},
            {"dry_branches_medium_01/dry_branches_medium_01_1k.gltf", 1000, 0.8f, 1.5f, Ground::Forest, 0, true, false,
             0.0f},
        }};
        constexpr const char* Panorama = "panoramas/autumn_forest_04_2k.hdr";
        constexpr std::array<const char*, 3> GroundTextures{"forest_ground_04/forest_ground_04_diff_2k.jpg",
                                                            "forest_ground_04/forest_ground_04_nor_gl_2k.jpg",
                                                            "forest_ground_04/forest_ground_04_arm_2k.jpg"};

        /** @brief One variant's copies: its full list, the handle the renderers draw, and the renderers' entities. */
        struct Variant
        {
            Fenrir::InstanceListData list;
            Fenrir::AssetHandle<Fenrir::InstanceList> handle;
        };

        /** @brief The forest's lists and what the panel and keys have set, kept as a scene resource. */
        struct ForestState
        {
            LightingSettings lighting;
            std::vector<Variant> variants;
            /** @brief The grass patches' entity, whose InstancedMeshRenderer M hides and shows. */
            Fenrir::Entity grass;
            std::size_t blades = 0;
            std::size_t patches = 0;
            std::size_t copies = 0;
            int density = 100;
            /** @brief The density the lists were last cut to. */
            int listedDensity = 100;
            bool fetched = false;
            bool grassVisible = true;
        };

        /** @brief A transform from a position, a turn and a uniform scale. */
        Fenrir::Math::Mat4 Placed(Vec3 position, Fenrir::Math::Quat rotation, float scale)
        {
            return glm::scale(Fenrir::Math::Translate(Fenrir::Math::Mat4(1.0f), position) * glm::mat4_cast(rotation),
                              Vec3(scale));
        }

        /**
         * @brief One square patch of `BladesPerPatch` grass blades standing on y 0, `PatchSize` metres a side round its
         * centre. Each blade is a tapering strip of three segments and a tip, five triangles, leaning a little one way
         * and curving as it rises, with its own height, width, turn and tint; UV0's u runs across it and v from its
         * root (0) to its tip (1), which grass.frag shades along and grass.vert sways by. Its normal faces across the
         * blade leaning up, so blades light softly from the sky; the material is double-sided.
         */
        Fenrir::MeshData GrassPatch(std::uint32_t seed)
        {
            std::mt19937 random(seed);
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            constexpr int Segments = 3;
            Fenrir::MeshData mesh;
            mesh.positions.reserve(std::size_t{BladesPerPatch} * (2 * Segments + 1));
            for (int blade = 0; blade < BladesPerPatch; ++blade)
            {
                const Vec3 root{(unit(random) - 0.5f) * PatchSize, 0.0f, (unit(random) - 0.5f) * PatchSize};
                // Mostly knee-low, a few taller stems.
                const float height = 0.22f + 0.5f * std::pow(unit(random), 1.6f);
                const float width = 0.02f + 0.025f * unit(random);
                const float turn = 2.0f * Pi * unit(random);
                const Vec3 side{std::cos(turn), 0.0f, std::sin(turn)};
                const float leanTurn = 2.0f * Pi * unit(random);
                const Vec3 lean = Vec3(std::cos(leanTurn), 0.0f, std::sin(leanTurn)) * (0.08f + 0.3f * unit(random));
                const Vec3 facing = glm::cross(side, Vec3(0.0f, 1.0f, 0.0f));
                const Vec3 normal = glm::normalize(facing + Vec3(0.0f, 0.6f, 0.0f));
                // Mostly green in a range of shades, one in seven dry and straw-coloured.
                const float shade = 0.65f + 0.35f * unit(random);
                const Fenrir::Colour tint = unit(random) < 0.14f
                                                ? Fenrir::Colour{shade, shade * 0.82f, shade * 0.45f, 1.0f}
                                                : Fenrir::Colour{shade * 0.9f, shade, shade * 0.8f, 1.0f};
                const auto first = static_cast<std::uint32_t>(mesh.positions.size());
                const auto add = [&](Vec3 position, float u, float v) {
                    mesh.positions.push_back(position);
                    mesh.normals.push_back(normal);
                    mesh.uv0.emplace_back(u, v);
                    mesh.colours.push_back(tint);
                };
                for (int s = 0; s <= Segments; ++s)
                {
                    const float t = static_cast<float>(s) / Segments;
                    // Curving over as it rises, lean by the square of the way up.
                    const Vec3 centre = root + lean * height * t * t + Vec3(0.0f, height * t, 0.0f);
                    if (s == Segments)
                    {
                        add(centre, 0.5f, 1.0f);
                        break;
                    }
                    const float half = 0.5f * width * (1.0f - 0.7f * t);
                    add(centre - side * half, 0.0f, t);
                    add(centre + side * half, 1.0f, t);
                }
                // Counter-clockwise seen from `facing`, so its front faces the way its normal leans.
                for (std::uint32_t s = 0; s < Segments - 1; ++s)
                {
                    const std::uint32_t left = first + 2 * s;
                    mesh.indices.insert(mesh.indices.end(), {left, left + 1, left + 2, left + 1, left + 3, left + 2});
                }
                const std::uint32_t last = first + 2 * (Segments - 1);
                mesh.indices.insert(mesh.indices.end(), {last, last + 1, last + 2});
            }
            return mesh;
        }

        /** @brief A flat square of ground `GroundHalf` metres each way round the origin, its UVs repeating. */
        Fenrir::MeshData GroundPlane()
        {
            Fenrir::MeshData mesh;
            const float repeats = 2.0f * GroundHalf / GroundTile;
            for (const Vec2 corner : {Vec2(-1.0f, -1.0f), Vec2(-1.0f, 1.0f), Vec2(1.0f, 1.0f), Vec2(1.0f, -1.0f)})
            {
                mesh.positions.emplace_back(corner.x * GroundHalf, 0.0f, corner.y * GroundHalf);
                mesh.normals.emplace_back(0.0f, 1.0f, 0.0f);
                // u along +X, v along +Z: the map's green (up the image, towards -v) is -Z, as cross(N, T) gives.
                mesh.tangents.emplace_back(1.0f, 0.0f, 0.0f, 1.0f);
                mesh.uv0.emplace_back((corner.x * 0.5f + 0.5f) * repeats, (corner.y * 0.5f + 0.5f) * repeats);
            }
            mesh.indices = {0, 1, 2, 0, 2, 3};
            return mesh;
        }

        /**
         * @brief A golden-hour sky made here, linear RGBA32F in the renderer's panorama mapping: deep blue overhead
         * warming to orange at the horizon, brightest towards the sun, with the sun's disc and glow where the
         * DirectionalLight comes from, and a dark ground below. The scene's sky without the fetched panorama.
         */
        Fenrir::ImageData GoldenHourSky(std::uint32_t width)
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
                    const float phi =
                        2.0f * Pi * ((static_cast<float>(column) + 0.5f) / static_cast<float>(width) - 0.5f);
                    const Vec3 direction{std::sin(theta) * std::sin(phi), std::cos(theta),
                                         -std::sin(theta) * std::cos(phi)};
                    const float sun = std::max(glm::dot(direction, toSun), 0.0f);
                    Vec3 colour;
                    if (direction.y >= 0.0f)
                    {
                        const float up = std::pow(direction.y, 0.45f);
                        colour = glm::mix(Vec3(1.1f, 0.55f, 0.25f), Vec3(0.12f, 0.22f, 0.5f), up);
                        colour += Vec3(1.2f, 0.6f, 0.25f) * std::pow(sun, 6.0f) * (1.0f - up * 0.6f);
                        colour += Vec3(60.0f, 42.0f, 25.0f) * std::pow(sun, 2000.0f);
                    }
                    else
                    {
                        colour = glm::mix(Vec3(0.12f, 0.08f, 0.05f), Vec3(0.03f, 0.03f, 0.02f),
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
         * @brief The ground's material: Poly Haven's Forest Ground 04 when it was fetched (base colour, GL normal map,
         * and its ARM map as occlusion, roughness and metal), else a plain earth colour.
         */
        Fenrir::AssetHandle<Fenrir::Material> GroundMaterial(Fenrir::AssetStore& store, Fenrir::Scene& scene,
                                                             bool& fetched)
        {
            const auto material = LitMaterial(store, scene, {0.16f, 0.12f, 0.07f, 1.0f}, 0.95f);
            std::array<Fenrir::AssetHandle<Fenrir::Image>, GroundTextures.size()> images;
            for (std::size_t i = 0; i < GroundTextures.size(); ++i)
            {
                const std::optional<std::string> path = FetchedAsset(GroundTextures.at(i));
                const auto image = path ? store.Load<Fenrir::Image>(*path, scene) : Fenrir::LoadResult<Fenrir::Image>{};
                if (!path || !image.Ok())
                {
                    fetched = false;
                    return material;
                }
                images.at(i) = image.handle;
            }
            const auto arm = AsLinear(store, scene, images[2]);
            store.SetColour(material, "baseColour", Fenrir::Colour::White);
            store.SetFloat(material, "roughness", 1.0f);
            store.SetTexture(material, "baseColourTexture", images[0]);
            store.SetTexture(material, "normalTexture", AsLinear(store, scene, images[1]));
            store.SetTexture(material, "metallicRoughnessTexture", arm);
            store.SetTexture(material, "occlusionTexture", arm);
            return material;
        }

        /** @brief Where on the ground a copy of a species growing on `ground` stands, or none to skip this one. */
        std::optional<Vec2> GroundSpot(std::mt19937& random, Ground ground)
        {
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            const float angle = 2.0f * Pi * unit(random);
            const Vec2 out{std::cos(angle), std::sin(angle)};
            if (ground == Ground::Forest)
            {
                const Vec2 toSun = glm::normalize(Vec2(-SunTravel.x, -SunTravel.z));
                if (std::acos(std::clamp(glm::dot(out, toSun), -1.0f, 1.0f)) < SunGap)
                {
                    return std::nullopt;
                }
                // Evenly over the ring's area from the clearing's edge to the forest's.
                const float inner = ClearingRadius + 2.0f;
                const float radius =
                    std::sqrt(inner * inner + (ForestOuter * ForestOuter - inner * inner) * unit(random));
                return out * radius;
            }
            // Over the whole disc, but a copy inside the clearing survives only with the chance of how far out it is.
            const float radius = ForestOuter * std::sqrt(unit(random));
            if (radius < ClearingRadius && unit(random) > 0.15f + 0.85f * std::pow(radius / ClearingRadius, 3.0f))
            {
                return std::nullopt;
            }
            return out * radius;
        }

        /**
         * @brief Each variant's list for `species` with `variants` variants: its copies placed on its ground, split
         * round-robin between the variants, each turned and scaled at random and tinted slightly, `rest` (each
         * variant's node rotation and scale) applied under the placement.
         */
        std::vector<Fenrir::InstanceListData> Plant(const Species& species, std::uint32_t seed,
                                                    const std::vector<Fenrir::Math::Mat4>& rests)
        {
            std::mt19937 random(seed);
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            std::uniform_real_distribution<float> symmetric(-1.0f, 1.0f);
            std::vector<Fenrir::InstanceListData> lists(rests.size());
            std::vector<Vec2> placed;
            // Rejection gives up on a copy after a few misses, so a crowded species simply grows fewer.
            for (int i = 0, tries = 0; i < species.count && tries < species.count * 8; ++tries)
            {
                const std::optional<Vec2> spot = GroundSpot(random, species.ground);
                if (!spot)
                {
                    continue;
                }
                if (species.spacing > 0.0f && std::ranges::any_of(placed, [&](Vec2 other) {
                        return glm::distance(other, *spot) < species.spacing;
                    }))
                {
                    continue;
                }
                placed.push_back(*spot);
                const std::size_t variant = static_cast<std::size_t>(i) % rests.size();
                // Small more often than large.
                const float scale =
                    species.minScale + (species.maxScale - species.minScale) * std::pow(unit(random), 2.0f);
                Fenrir::Math::Quat rotation = Fenrir::Math::AngleAxis(2.0f * Pi * unit(random), Vec3(0.0f, 1.0f, 0.0f));
                if (species.tumbled)
                {
                    Vec3 axis{symmetric(random), symmetric(random), symmetric(random)};
                    axis = glm::length(axis) > 0.01f ? glm::normalize(axis) : Vec3(0.0f, 1.0f, 0.0f);
                    rotation = Fenrir::Math::AngleAxis(2.0f * Pi * unit(random), axis);
                }
                // Sunk a little, so tumbled and leaning copies do not float.
                const Vec3 position{spot->x, species.tumbled ? -0.02f * scale : -0.01f, spot->y};
                lists[variant].transforms.push_back(Placed(position, rotation, scale) * rests[variant]);
                const float light = 0.85f + 0.15f * unit(random);
                lists[variant].colours.push_back({light, light, light * (0.92f + 0.08f * unit(random)), 1.0f});
                ++i;
            }
            return lists;
        }

        /** @brief `list`'s first `percent` percent of copies, at least one if it has any. */
        Fenrir::InstanceListData Thinned(const Fenrir::InstanceListData& list, int percent)
        {
            const std::size_t total = list.transforms.size();
            const std::size_t count = std::min(
                total, std::max<std::size_t>(total > 0 ? 1 : 0, total * static_cast<std::size_t>(percent) / 100));
            Fenrir::InstanceListData cut;
            cut.transforms.assign(list.transforms.begin(),
                                  list.transforms.begin() + static_cast<std::ptrdiff_t>(count));
            cut.colours.assign(list.colours.begin(), list.colours.begin() + static_cast<std::ptrdiff_t>(count));
            return cut;
        }

        /** @brief One part a variant draws: a mesh with its material. */
        struct Part
        {
            Fenrir::AssetHandle<Fenrir::Mesh> mesh;
            Fenrir::AssetHandle<Fenrir::Material> material;
        };

        /**
         * @brief Plants `species` as children of `forest`: one list per variant (its model's nodes that draw), one
         * InstancedMeshRenderer per mesh of the variant, drawing that list. False if the model did not load.
         */
        bool SpawnSpecies(Fenrir::AssetStore& store, Fenrir::Scene& scene, Fenrir::Entity forest, ForestState& state,
                          const Species& species, const std::string& path, std::uint32_t seed)
        {
            const auto loaded = store.Load<Fenrir::Model>(path, scene);
            const Fenrir::Model* model = loaded.Ok() ? store.TryGet(loaded.handle) : nullptr;
            if (model == nullptr)
            {
                return false;
            }
            std::vector<std::vector<Part>> variants;
            std::vector<Fenrir::Math::Mat4> rests;
            for (const Fenrir::ModelNode& node : model->nodes)
            {
                if (node.meshes.empty())
                {
                    continue;
                }
                std::vector<Part>& parts = variants.emplace_back();
                for (const Fenrir::ModelMesh& part : node.meshes)
                {
                    auto mesh = part.mesh;
                    if (species.simplifyCells > 0)
                    {
                        if (const Fenrir::MeshData* full = store.TryGet(part.mesh))
                        {
                            mesh = store.Add<Fenrir::Mesh>(Simplified(*full, species.simplifyCells), scene);
                        }
                    }
                    parts.push_back({mesh, part.material});
                }
                // The node's own turn and scale; its position only lays the variants out side by side in the file.
                rests.push_back(glm::scale(glm::mat4_cast(node.rest.rot), node.rest.scale));
            }
            if (variants.empty())
            {
                return false;
            }
            std::vector<Fenrir::InstanceListData> lists = Plant(species, seed, rests);
            Fenrir::EntityList& entities = scene.GetEntityList();
            for (std::size_t v = 0; v < variants.size(); ++v)
            {
                state.copies += lists[v].transforms.size();
                const auto handle = store.Add<Fenrir::InstanceList>(lists[v], scene);
                for (const Part& part : variants[v])
                {
                    Fenrir::Entity entity = entities.CreateEntity();
                    entity.AddComponent<Fenrir::Transform>();
                    entity.AddComponent<Fenrir::InstancedMeshRenderer>(
                        Fenrir::InstancedMeshRenderer{part.mesh, part.material, handle, species.castShadows});
                    forest.AddChild(entity);
                }
                state.variants.push_back({std::move(lists[v]), handle});
            }
            return true;
        }

        /**
         * @brief Stand-ins for the fetched plants, from the committed cube and sphere: grey boulders over the ground
         * and tall dark trunks in the forest, two instance lists, so the scene still has shadows to cast on the grass.
         */
        void SpawnStandIns(Fenrir::AssetStore& store, Fenrir::Scene& scene, Fenrir::Entity forest, ForestState& state)
        {
            const auto cube = store.Load<Fenrir::Mesh>("cube.obj#Mesh0", scene);
            const auto sphere = store.Load<Fenrir::Mesh>("sphere.obj#Mesh0", scene);
            if (!cube.Ok() || !sphere.Ok())
            {
                return;
            }
            const Species boulders{"", 300, 0.3f, 1.5f, Ground::Anywhere, 0, true, false, 0.0f};
            const Species trunks{"", 80, 1.0f, 1.0f, Ground::Forest, 0, true, false, 4.0f};
            const std::array<Part, 2> parts{
                {{sphere.handle, LitMaterial(store, scene, {0.4f, 0.38f, 0.35f, 1.0f}, 0.9f)},
                 {cube.handle, LitMaterial(store, scene, {0.2f, 0.13f, 0.08f, 1.0f}, 0.9f)}}};
            // Both meshes are a unit across round their middle: a trunk is 0.5 by 7 metres, standing on the ground.
            const std::array<Fenrir::Math::Mat4, 2> rests{
                glm::scale(Fenrir::Math::Mat4(1.0f), Vec3(1.0f)),
                glm::scale(Fenrir::Math::Translate(Fenrir::Math::Mat4(1.0f), Vec3(0.0f, 3.5f, 0.0f)),
                           Vec3(0.5f, 7.0f, 0.5f))};
            Fenrir::EntityList& entities = scene.GetEntityList();
            for (std::size_t i = 0; i < parts.size(); ++i)
            {
                std::vector<Fenrir::InstanceListData> lists =
                    Plant(i == 0 ? boulders : trunks, static_cast<std::uint32_t>(101 + i), {rests.at(i)});
                state.copies += lists[0].transforms.size();
                const auto handle = store.Add<Fenrir::InstanceList>(lists[0], scene);
                Fenrir::Entity entity = entities.CreateEntity();
                entity.AddComponent<Fenrir::Transform>();
                entity.AddComponent<Fenrir::InstancedMeshRenderer>(
                    Fenrir::InstancedMeshRenderer{parts.at(i).mesh, parts.at(i).material, handle, true});
                forest.AddChild(entity);
                state.variants.push_back({std::move(lists[0]), handle});
            }
        }

        /**
         * @brief The grass: one generated patch mesh drawn by one InstancedMeshRenderer at every grid cell within
         * `GrassRadius`, each copy turned a quarter-turn at random so the repeat does not show, through the demo's wind
         * shader. Logs an error and spawns nothing if the shader or material cannot be made.
         */
        void SpawnGrass(Fenrir::App& app, Fenrir::Scene& scene, ForestState& state)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            Fenrir::ShaderData shader;
            const auto bytes = std::as_bytes(std::span(Fenrir::Shaders::LightingDemoGrass));
            shader.bytes.assign(bytes.begin(), bytes.end());
            const auto material = store.CreateMaterial(store.Add<Fenrir::Shader>(std::move(shader), scene), scene);
            if (material.IsEmpty())
            {
                app.Logger()->Error("{}: the grass shader did not load", SceneName);
                return;
            }
            store.SetDoubleSided(material, true);
            const Fenrir::MeshData patch = GrassPatch(17);
            const auto mesh = store.Add<Fenrir::Mesh>(patch, scene);

            std::mt19937 random(23);
            std::uniform_int_distribution<int> quarter(0, 3);
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            Fenrir::InstanceListData patches;
            const int cells = static_cast<int>(std::ceil(GrassRadius / PatchSize));
            for (int x = -cells; x < cells; ++x)
            {
                for (int z = -cells; z < cells; ++z)
                {
                    const Vec3 centre{(static_cast<float>(x) + 0.5f) * PatchSize, 0.0f,
                                      (static_cast<float>(z) + 0.5f) * PatchSize};
                    if (glm::length(Vec2(centre.x, centre.z)) > GrassRadius)
                    {
                        continue;
                    }
                    const auto turn = Fenrir::Math::AngleAxis(0.5f * Pi * static_cast<float>(quarter(random)),
                                                              Vec3(0.0f, 1.0f, 0.0f));
                    patches.transforms.push_back(Placed(centre, turn, 1.0f));
                    const float lush = 0.85f + 0.15f * unit(random);
                    patches.colours.push_back({lush, lush, lush * 0.9f, 1.0f});
                }
            }
            state.patches = patches.transforms.size();
            state.blades = state.patches * BladesPerPatch;
            const auto list = store.Add<Fenrir::InstanceList>(std::move(patches), scene);
            state.grass = scene.GetEntityList().CreateEntity();
            state.grass.AddComponent<Fenrir::Transform>();
            state.grass.AddComponent<Fenrir::InstancedMeshRenderer>(
                Fenrir::InstancedMeshRenderer{mesh, material, list});
        }

        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            ForestState state;
            // Shadows across the clearing and into the trees round it; the forest beyond is in the far cascade's dusk.
            state.lighting.sunShadows.shadowDistance = 70.0f;
            bool fetched = true;
            if (const std::optional<std::string> path = FetchedAsset(Panorama))
            {
                if (const auto image = store.Load<Fenrir::Image>(*path, scene); image.Ok())
                {
                    state.lighting.panoramas.push_back({"Autumn Forest 04 (2k)", image.handle});
                }
            }
            fetched = fetched && !state.lighting.panoramas.empty();
            state.lighting.panoramas.push_back(
                {"Procedural golden hour", store.Add<Fenrir::Image>(GoldenHourSky(1024), scene)});

            Fenrir::EntityList& entities = scene.GetEntityList();
            Fenrir::Entity sun = entities.CreateEntity();
            sun.AddComponent<Fenrir::Transform>().rot = glm::quatLookAt(SunTravel, Vec3(0.0f, 1.0f, 0.0f));
            sun.AddComponent<Fenrir::DirectionalLight>(Fenrir::DirectionalLight{SunColour, SunIntensity});

            Fenrir::Entity ground = entities.CreateEntity();
            ground.AddComponent<Fenrir::Transform>();
            ground.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{
                store.Add<Fenrir::Mesh>(GroundPlane(), scene), GroundMaterial(store, scene, fetched)});

            SpawnGrass(app, scene, state);

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
            fetched = fetched && paths.size() == ForestSpecies.size();
            if (fetched)
            {
                for (std::size_t i = 0; i < ForestSpecies.size(); ++i)
                {
                    if (!SpawnSpecies(store, scene, forest, state, ForestSpecies.at(i), paths.at(i),
                                      static_cast<std::uint32_t>(41 + i)))
                    {
                        app.Logger()->Error("{}: {} did not load from the cache", SceneName, ForestSpecies.at(i).model);
                    }
                }
            }
            else
            {
                SpawnStandIns(store, scene, forest, state);
                app.Logger()->Info("{}: the plants, rocks, ground and panorama are fetched assets "
                                   "(FENRIR_FETCH_DEMO_ASSETS); the grass grows, and boulders and trunks stand in",
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
            RunLightingPanel(app, scene, state->lighting, [&] {
                ImGui::SliderInt("Forest density % (N)", &state->density, 5, 100);
                ImGui::Checkbox("Grass (M)", &state->grassVisible);
                ImGui::Text("Forest: %zu copies in %zu lists", state->copies, state->variants.size());
                ImGui::Text("Grass: %zu blades in %zu patches", state->blades, state->patches);
                ImGui::Text("Draws: %u  Instances: %u  Shadow draws: %u", stats.draws, stats.instances,
                            stats.shadowDraws);
            });

            // A new density cuts each list again: Replace bumps its revision, so the renderer uploads it once more.
            if (state->density != state->listedDensity)
            {
                Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
                for (const Variant& variant : state->variants)
                {
                    store.Replace(variant.handle, Thinned(variant.list, state->density));
                }
                state->listedDensity = state->density;
            }
            if (state->grass.IsValid())
            {
                state->grass.GetComponent<Fenrir::InstancedMeshRenderer>().visible = state->grassVisible;
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
        // At the clearing's edge, standing in the grass, looking across it with the low sun ahead and to the right.
        scene.camera.position = {-2.0f, 1.6f, 15.0f};
        scene.camera.yaw = 0.16f;
        scene.camera.pitch = -0.08f;
        scene.spawn = Spawn;
        scene.update = Update;
        return scene;
    }
} // namespace LightingDemo
