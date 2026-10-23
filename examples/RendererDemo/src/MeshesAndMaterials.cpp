#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirRenderer/SpawnModel.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirShaders/RendererDemoStripes.hpp"
#include "RendererScenes.hpp"

namespace RendererDemo
{
    namespace
    {
        using Fenrir::Math::Vec3;
        using Material = Fenrir::AssetHandle<Fenrir::Material>;

        constexpr float Pi = 3.14159265f;
        // The ambient light's intensities M cycles through: the default dim grey, bright, and off.
        constexpr std::array<float, 3> AmbientSteps{1.0f, 4.0f, 0.0f};
        // The shared material's colours C cycles through, sRGB.
        constexpr std::array<std::uint32_t, 3> SharedColours{0xD4A017FF, 0x20B2AAFF, 0xC0203AFF};

        /** @brief What the keys change, kept as a scene resource. */
        struct MaterialsState
        {
            Fenrir::Entity light;
            Fenrir::Entity backpack;
            /** @brief The two quads turning about +Y: double-sided, and single-sided beside it. */
            std::array<Fenrir::Entity, 2> turning;
            /** @brief The material the row of three cubes shares. */
            Material shared;
            float lightYaw = 0.5f;
            std::size_t ambient = 0;
            std::size_t colour = 0;
            bool backpackVisible = true;
            float seconds = 0.0f;
        };

        Fenrir::Math::Quat YawPitch(float yaw, float pitch)
        {
            return Fenrir::Math::AngleAxis(yaw, Vec3(0.0f, 1.0f, 0.0f)) *
                   Fenrir::Math::AngleAxis(pitch, Vec3(1.0f, 0.0f, 0.0f));
        }

        /**
         * @brief A quad of `width` x `height` in the XY plane facing +Z, with UV (0, 0) at its top-left corner and
         * `repeat` at its bottom-right.
         */
        Fenrir::MeshData Quad(float width, float height, float repeat = 1.0f)
        {
            Fenrir::MeshData mesh;
            const float w = width * 0.5f;
            const float h = height * 0.5f;
            mesh.positions = {Vec3(-w, -h, 0.0f), Vec3(w, -h, 0.0f), Vec3(w, h, 0.0f), Vec3(-w, h, 0.0f)};
            mesh.normals = {Vec3(0, 0, 1), Vec3(0, 0, 1), Vec3(0, 0, 1), Vec3(0, 0, 1)};
            mesh.uv0 = {{0.0f, repeat}, {repeat, repeat}, {repeat, 0.0f}, {0.0f, 0.0f}};
            mesh.indices = {0, 1, 2, 0, 2, 3};
            return mesh;
        }

        /** @brief Appends one RGBA8 pixel to `image`. */
        void Push(Fenrir::ImageData& image, std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha)
        {
            for (const std::uint8_t channel : {red, green, blue, alpha})
            {
                image.pixels.push_back(std::byte{channel});
            }
        }

        /**
         * @brief A `size` x `size` sRGB image of a white disc fading out to its edge: in alpha over white for
         * `inAlpha`, else in colour over opaque black.
         */
        Fenrir::ImageData Disc(std::uint32_t size, bool inAlpha)
        {
            Fenrir::ImageData image;
            image.width = size;
            image.height = size;
            for (std::uint32_t y = 0; y < size; ++y)
            {
                for (std::uint32_t x = 0; x < size; ++x)
                {
                    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
                    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
                    const float fade = std::clamp(1.0f - std::sqrt(u * u + v * v), 0.0f, 1.0f);
                    const auto level = static_cast<std::uint8_t>(std::lround(fade * 255.0f));
                    if (inAlpha)
                    {
                        Push(image, 255, 255, 255, level);
                    }
                    else
                    {
                        Push(image, level, level, level, 255);
                    }
                }
            }
            return image;
        }

        /** @brief An 8 x 8 checkerboard of two sRGB greys. */
        Fenrir::ImageData Checker()
        {
            Fenrir::ImageData image;
            image.width = 8;
            image.height = 8;
            for (std::uint32_t y = 0; y < 8; ++y)
            {
                for (std::uint32_t x = 0; x < 8; ++x)
                {
                    const std::uint8_t level = (x + y) % 2 == 0 ? 220 : 60;
                    Push(image, level, level, level, 255);
                }
            }
            return image;
        }

        /** @brief Shows or hides every MeshRenderer at or below `entity`. */
        void SetVisible(const Fenrir::Entity& entity, bool visible)
        {
            if (entity.HasComponent<Fenrir::MeshRenderer>())
            {
                entity.GetComponent<Fenrir::MeshRenderer>().visible = visible;
            }
            entity.ForEachChild([&](const Fenrir::Entity& child) { SetVisible(child, visible); });
        }

        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            // The scene owns what it loads and makes, so its assets go when it is left.
            const auto cube = store.Load<Fenrir::Mesh>("cube.obj#Mesh0", scene);
            const auto sphere = store.Load<Fenrir::Mesh>("sphere.obj#Mesh0", scene);
            const auto backpack = store.Load<Fenrir::Model>("backpack/backpack.obj", scene);
            if (!cube.Ok() || !sphere.Ok() || !backpack.Ok())
            {
                app.Logger()->Error("Meshes and materials: the cube, sphere or backpack did not load from the asset "
                                    "root");
                return;
            }
            const auto lit = store.Load<Fenrir::Shader>("builtin:lit", scene).handle;
            const auto unlit = store.Load<Fenrir::Shader>("builtin:unlit", scene).handle;
            const auto make = [&](Fenrir::AssetHandle<Fenrir::Shader> shader, std::uint32_t srgb) {
                const Material made = store.CreateMaterial(shader, scene);
                store.SetColour(made, "baseColour", Fenrir::Colour::FromSrgb(srgb));
                return made;
            };

            Fenrir::EntityList& entities = scene.GetEntityList();
            const auto place = [&](Fenrir::AssetHandle<Fenrir::Mesh> mesh, Material material, const Vec3& position,
                                   const Vec3& scale = Vec3(1.0f)) {
                Fenrir::Entity entity = entities.CreateEntity();
                auto& transform = entity.AddComponent<Fenrir::Transform>();
                transform.pos = position;
                transform.scale = scale;
                entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{mesh, material});
                return entity;
            };
            MaterialsState state;

            // The ground.
            place(cube.handle, make(lit, 0x606060FF), {0.0f, -0.1f, 0.0f}, {22.0f, 0.2f, 14.0f});

            // The imported backpack, spawned as a node tree of MeshRenderers.
            state.backpack = Fenrir::SpawnModel(store, scene, backpack.handle);
            auto& backpackTransform = state.backpack.GetComponent<Fenrir::Transform>();
            backpackTransform.pos = {-7.6f, 0.9f, -0.5f};
            backpackTransform.rot = YawPitch(0.6f, 0.0f);

            // Primitives in builtin:unlit, then in builtin:lit.
            place(cube.handle, make(unlit, 0x3070E0FF), {-4.2f, 0.5f, 0.0f});
            place(sphere.handle, make(unlit, 0x3070E0FF), {-3.0f, 0.5f, 0.0f});
            place(cube.handle, make(lit, 0x3070E0FF), {-1.6f, 0.5f, 0.0f});
            place(sphere.handle, make(lit, 0x3070E0FF), {-0.4f, 0.5f, 0.0f});

            // A user's own shader, from RendererDemo's sources, on a cube.
            Fenrir::ShaderData stripesData;
            const auto bytes = std::as_bytes(std::span(Fenrir::Shaders::RendererDemoStripes));
            stripesData.bytes.assign(bytes.begin(), bytes.end());
            const auto stripes = store.Add<Fenrir::Shader>(std::move(stripesData), scene);
            place(cube.handle, store.CreateMaterial(stripes, scene), {1.0f, 0.5f, 0.0f});

            // The fallback: a MeshRenderer with no material draws flat magenta, logged once.
            place(cube.handle, Material{}, {2.4f, 0.5f, 0.0f});
            // An empty mesh and a stale one beside it draw nothing; the stale one is logged once.
            place(Fenrir::AssetHandle<Fenrir::Mesh>{}, make(lit, 0xFFFFFFFF), {3.6f, 0.5f, 0.0f});
            const auto stale = store.Add<Fenrir::Mesh>(Quad(1.0f, 1.0f), scene);
            store.Unload(stale);
            place(stale, make(lit, 0xFFFFFFFF), {3.6f, 0.5f, 0.0f});

            // Three cubes sharing one material, which C edits.
            state.shared = make(lit, SharedColours.at(0));
            for (const float x : {4.8f, 6.0f, 7.2f})
            {
                place(cube.handle, state.shared, {x, 0.5f, 0.0f});
            }

            // The metallic/roughness grid: metallic 0 to 1 from left to right, roughness 0.1 to 1 from bottom to top.
            const Fenrir::Colour gold = Fenrir::Colour::FromSrgb(0xE0B060FF);
            for (int row = 0; row < 4; ++row)
            {
                for (int column = 0; column < 5; ++column)
                {
                    const Material ball = store.CreateMaterial(lit, scene);
                    store.SetColour(ball, "baseColour", gold);
                    store.SetFloat(ball, "metallic", static_cast<float>(column) / 4.0f);
                    store.SetFloat(ball, "roughness", 0.1f + 0.3f * static_cast<float>(row));
                    const Vec3 position(3.0f + 1.2f * static_cast<float>(column), 1.0f + 1.2f * static_cast<float>(row),
                                        -4.0f);
                    place(sphere.handle, ball, position, Vec3(0.9f));
                }
            }

            // The four blend modes side by side in front of a checkerboard: Opaque, Masked, AlphaBlend and Additive,
            // each a disc fading out to its edge.
            Fenrir::SamplerDescription nearest;
            nearest.filter = Fenrir::SamplerFilter::Nearest;
            const Material board = make(lit, 0xFFFFFFFF);
            store.SetTexture(board, "baseColourTexture", store.Add<Fenrir::Image>(Checker(), scene), nearest);
            place(store.Add<Fenrir::Mesh>(Quad(7.0f, 2.0f, 4.0f), scene), board, {-2.5f, 2.2f, -2.4f});
            const auto alphaDisc = store.Add<Fenrir::Image>(Disc(64, true), scene);
            const auto glowDisc = store.Add<Fenrir::Image>(Disc(64, false), scene);
            const auto discQuad = store.Add<Fenrir::Mesh>(Quad(1.4f, 1.4f), scene);
            const std::array<std::pair<Fenrir::BlendMode, std::uint32_t>, 4> modes{{
                {Fenrir::BlendMode::Opaque, 0xE04040FF},
                {Fenrir::BlendMode::Masked, 0x40C040FF},
                {Fenrir::BlendMode::AlphaBlend, 0x4060E0FF},
                {Fenrir::BlendMode::Additive, 0xE0A020FF},
            }};
            for (std::size_t i = 0; i < modes.size(); ++i)
            {
                const auto& [mode, srgb] = modes.at(i);
                const Material disc = make(unlit, srgb);
                store.SetBlendMode(disc, mode);
                store.SetTexture(disc, "baseColourTexture", mode == Fenrir::BlendMode::Additive ? glowDisc : alphaDisc);
                place(discQuad, disc, {-5.1f + 1.75f * static_cast<float>(i), 2.2f, -2.0f});
            }

            // A double-sided quad turning about +Y, and a single-sided one beside it, which vanishes while it faces
            // away.
            const auto turningQuad = store.Add<Fenrir::Mesh>(Quad(1.2f, 1.2f), scene);
            const Material twoSided = make(lit, 0x8040C0FF);
            store.SetDoubleSided(twoSided, true);
            state.turning.at(0) = place(turningQuad, twoSided, {8.6f, 0.8f, 0.0f});
            state.turning.at(1) = place(turningQuad, make(lit, 0x8040C0FF), {10.2f, 0.8f, 0.0f});

            // The light shines along its -Z; L turns it about +Y.
            state.light = entities.CreateEntity();
            state.light.AddComponent<Fenrir::Transform>().rot = YawPitch(state.lightYaw, -0.8f);
            state.light.AddComponent<Fenrir::DirectionalLight>();
            scene.AddResource<Fenrir::AmbientLight>();
            scene.AddResource<MaterialsState>(state);
        }

        void Update(Fenrir::App& app, Fenrir::Scene& scene)
        {
            MaterialsState* state = scene.GetResource<MaterialsState>();
            if (state == nullptr)
            {
                return;
            }
            state->seconds += static_cast<float>(app.GetTime().deltaTime);
            for (const Fenrir::Entity& quad : state->turning)
            {
                quad.GetComponent<Fenrir::Transform>().rot = YawPitch(state->seconds * 0.8f, 0.0f);
            }

            const auto& keyboard = app.GetResource<Fenrir::Keyboard>();
            if (keyboard.JustPressed(Fenrir::Key::L))
            {
                state->lightYaw += Pi / 4.0f;
                state->light.GetComponent<Fenrir::Transform>().rot = YawPitch(state->lightYaw, -0.8f);
                app.Logger()->Info("L: the light turned to {:.0f} degrees about +Y", state->lightYaw * 180.0f / Pi);
            }
            if (keyboard.JustPressed(Fenrir::Key::M))
            {
                state->ambient = (state->ambient + 1) % AmbientSteps.size();
                scene.GetResource<Fenrir::AmbientLight>()->intensity = AmbientSteps.at(state->ambient);
                app.Logger()->Info("M: the ambient light's intensity is {}", AmbientSteps.at(state->ambient));
            }
            if (keyboard.JustPressed(Fenrir::Key::V))
            {
                state->backpackVisible = !state->backpackVisible;
                SetVisible(state->backpack, state->backpackVisible);
                app.Logger()->Info("V: the backpack's MeshRenderers are {}",
                                   state->backpackVisible ? "visible" : "hidden");
            }
            if (keyboard.JustPressed(Fenrir::Key::C))
            {
                state->colour = (state->colour + 1) % SharedColours.size();
                Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
                store.SetColour(state->shared, "baseColour", Fenrir::Colour::FromSrgb(SharedColours.at(state->colour)));
                app.Logger()->Info("C: the three cubes' shared material changed colour");
            }
        }
    } // namespace

    FenrirDemo::DemoScene MeshesAndMaterials()
    {
        FenrirDemo::DemoScene scene;
        scene.name = "Meshes and materials";
        scene.keyHelp = "L: turn the light. M: the ambient light (dim, bright, off). V: show or hide the backpack. C: "
                        "the colour of the three cubes' shared material.";
        scene.keys = {Fenrir::Key::L, Fenrir::Key::M, Fenrir::Key::V, Fenrir::Key::C,
                      Fenrir::Key::L, Fenrir::Key::M, Fenrir::Key::V, Fenrir::Key::C};
        scene.camera.position = {0.0f, 3.0f, 11.0f};
        scene.camera.pitch = -0.2f;
        scene.spawn = Spawn;
        scene.update = Update;
        return scene;
    }
} // namespace RendererDemo
