#include <array>
#include <cmath>
#include <span>
#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirShaders/RendererDemoMonitor.hpp"
#include "RendererScenes.hpp"

namespace RendererDemo
{
    namespace
    {
        using Fenrir::Math::Vec3;

        constexpr float Pi = 3.14159265f;
        // The monitor camera's image: 16:9, drawn by the third camera and shown on the monitor's screen.
        constexpr std::uint32_t MonitorWidth = 480;
        constexpr std::uint32_t MonitorHeight = 270;
        // The minimap's order over the main views, and under them once O sends it below.
        constexpr std::int32_t MinimapOver = 10;
        constexpr std::int32_t MinimapUnder = -10;
        // The main views' orthographic height, in metres, once P switches their projection.
        constexpr float OrthographicHeight = 12.0f;

        /** @brief The two main views' viewports: one camera, side by side, or one over the other. */
        enum class Layout : std::uint8_t
        {
            Single,
            SideBySide,
            Stacked,
            Count,
        };

        /** @brief The scene's cameras and what the keys have set, kept as a scene resource. */
        struct CamerasState
        {
            Fenrir::Entity main;
            Fenrir::Entity second;
            Fenrir::Entity minimap;
            Fenrir::Entity monitor;
            Fenrir::Entity spinner;
            Layout layout = Layout::Single;
            bool minimapOver = true;
            bool orthographic = false;
            float seconds = 0.0f;
        };

        Fenrir::Math::Quat YawPitch(float yaw, float pitch)
        {
            return Fenrir::Math::AngleAxis(yaw, Vec3(0.0f, 1.0f, 0.0f)) *
                   Fenrir::Math::AngleAxis(pitch, Vec3(1.0f, 0.0f, 0.0f));
        }

        /** @brief A quad of `width` x `height` in the XY plane facing +Z, with UV (0, 0) at its top-left corner. */
        Fenrir::MeshData Screen(float width, float height)
        {
            Fenrir::MeshData mesh;
            const float w = width * 0.5f;
            const float h = height * 0.5f;
            mesh.positions = {Vec3(-w, -h, 0.0f), Vec3(w, -h, 0.0f), Vec3(w, h, 0.0f), Vec3(-w, h, 0.0f)};
            mesh.normals = {Vec3(0, 0, 1), Vec3(0, 0, 1), Vec3(0, 0, 1), Vec3(0, 0, 1)};
            mesh.uv0 = {{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}};
            mesh.indices = {0, 1, 2, 0, 2, 3};
            return mesh;
        }

        /** @brief Applies the layout to the two main views. */
        void ApplyLayout(CamerasState& state)
        {
            Fenrir::Camera& main = state.main.GetComponent<Fenrir::Camera>();
            Fenrir::Camera& second = state.second.GetComponent<Fenrir::Camera>();
            switch (state.layout)
            {
            case Layout::SideBySide:
                main.viewport = {0.0f, 0.0f, 0.5f, 1.0f};
                second.viewport = {0.5f, 0.0f, 0.5f, 1.0f};
                second.enabled = true;
                break;
            case Layout::Stacked:
                main.viewport = {0.0f, 0.0f, 1.0f, 0.5f};
                second.viewport = {0.0f, 0.5f, 1.0f, 0.5f};
                second.enabled = true;
                break;
            default:
                main.viewport = {};
                second.enabled = false;
                break;
            }
        }

        /** @brief Gives both main views the projection the P key chose. */
        void ApplyProjection(const CamerasState& state)
        {
            for (const Fenrir::Entity& entity : {state.main, state.second})
            {
                Fenrir::Camera& camera = entity.GetComponent<Fenrir::Camera>();
                if (state.orthographic)
                {
                    camera.projection = Fenrir::Orthographic{OrthographicHeight, 0.1f, 200.0f};
                }
                else
                {
                    camera.projection = Fenrir::Perspective{};
                }
            }
        }

        Fenrir::Entity AddCamera(Fenrir::EntityList& entities, const Vec3& position, const Fenrir::Math::Quat& rotation,
                                 const Fenrir::Camera& camera)
        {
            Fenrir::Entity entity = entities.CreateEntity();
            auto& transform = entity.AddComponent<Fenrir::Transform>();
            transform.pos = position;
            transform.rot = rotation;
            entity.AddComponent<Fenrir::Camera>(camera);
            return entity;
        }

        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            // The scene owns what it loads and makes, so its assets go when it is left.
            const Fenrir::LoadResult<Fenrir::Mesh> cube = store.Load<Fenrir::Mesh>("cube.obj#Mesh0", scene);
            if (!cube.Ok())
            {
                app.Logger()->Error("Cameras and render targets: the cube did not load from the asset root");
                return;
            }
            const auto lit = store.Load<Fenrir::Shader>("builtin:lit", scene).handle;
            const auto material = [&](std::uint32_t srgb) {
                const auto made = store.CreateMaterial(lit, scene);
                store.SetColour(made, "baseColour", Fenrir::Colour::FromSrgb(srgb));
                return made;
            };

            Fenrir::EntityList& entities = scene.GetEntityList();
            const auto box = [&](const Vec3& position, const Vec3& scale, Fenrir::AssetHandle<Fenrir::Material> paint) {
                Fenrir::Entity entity = entities.CreateEntity();
                auto& transform = entity.AddComponent<Fenrir::Transform>();
                transform.pos = position;
                transform.scale = scale;
                entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{cube.handle, paint});
                return entity;
            };

            // The ground, a ring of coloured boxes, and a spinning box in the middle.
            box({0.0f, -0.1f, 0.0f}, {16.0f, 0.2f, 16.0f}, material(0x8C8C8CFF));
            constexpr std::array<std::uint32_t, 6> colours{0xD04040FF, 0xE0A030FF, 0xD0D040FF,
                                                           0x40B040FF, 0x4070E0FF, 0x9050D0FF};
            for (std::size_t i = 0; i < colours.size(); ++i)
            {
                const float angle = 2.0f * Pi * static_cast<float>(i) / static_cast<float>(colours.size());
                box({4.0f * std::cos(angle), 0.5f, 4.0f * std::sin(angle)}, {1.0f, 1.0f, 1.0f},
                    material(colours.at(i)));
            }
            CamerasState state;
            state.spinner = box({0.0f, 1.0f, 0.0f}, {1.5f, 1.5f, 1.5f}, material(0xF0F0F0FF));

            // The monitor: a frame and a screen showing the monitor camera's image, a render target (ADR-0026).
            Fenrir::ImageData target;
            target.width = MonitorWidth;
            target.height = MonitorHeight;
            const auto image = store.Add<Fenrir::Image>(std::move(target), scene);
            Fenrir::ShaderData monitorShader;
            const auto bytes = std::as_bytes(std::span(Fenrir::Shaders::RendererDemoMonitor));
            monitorShader.bytes.assign(bytes.begin(), bytes.end());
            const auto screenMaterial =
                store.CreateMaterial(store.Add<Fenrir::Shader>(std::move(monitorShader), scene), scene);
            store.SetTexture(screenMaterial, "screen", image);
            box({0.0f, 2.6f, -6.15f}, {4.4f, 2.65f, 0.2f}, material(0x202020FF));
            Fenrir::Entity screen = entities.CreateEntity();
            screen.AddComponent<Fenrir::Transform>().pos = {0.0f, 2.6f, -6.0f};
            screen.AddComponent<Fenrir::MeshRenderer>(
                Fenrir::MeshRenderer{store.Add<Fenrir::Mesh>(Screen(4.0f, 2.25f), scene), screenMaterial});

            Fenrir::Entity light = entities.CreateEntity();
            light.AddComponent<Fenrir::Transform>().rot = YawPitch(0.5f, -0.9f);
            light.AddComponent<Fenrir::DirectionalLight>();
            scene.AddResource<Fenrir::Sky>(Fenrir::ColourSky{Fenrir::Colour{0.1f, 0.1f, 0.1f, 1.0f}});

            // The main view, a second view for the split layouts, an orthographic top-down minimap over them, and the
            // monitor's camera drawing into the image.
            Fenrir::Camera main;
            state.main = AddCamera(entities, {0.0f, 4.0f, 9.0f}, YawPitch(0.0f, -0.35f), main);
            Fenrir::Camera second;
            second.enabled = false;
            // Over the main view where they meet, so no two views into the window tie.
            second.order = 1;
            state.second = AddCamera(entities, {9.0f, 4.0f, 0.0f}, YawPitch(Pi * 0.5f, -0.35f), second);
            Fenrir::Camera minimap;
            minimap.projection = Fenrir::Orthographic{18.0f, 0.1f, 50.0f};
            minimap.order = MinimapOver;
            minimap.viewport = {0.74f, 0.03f, 0.23f, 0.36f};
            minimap.background = Fenrir::ColourBackground{Fenrir::Colour{0.02f, 0.04f, 0.1f, 1.0f}};
            state.minimap = AddCamera(entities, {0.0f, 20.0f, 0.0f}, YawPitch(0.0f, -Pi * 0.5f), minimap);
            Fenrir::Camera monitor;
            monitor.target = Fenrir::ImageTarget{image};
            monitor.background = Fenrir::ColourBackground{Fenrir::Colour{0.05f, 0.12f, 0.12f, 1.0f}};
            state.monitor = AddCamera(entities, {7.0f, 3.0f, 0.0f}, YawPitch(Pi * 0.5f, -0.3f), monitor);

            ApplyLayout(state);
            scene.AddResource<CamerasState>(state);
        }

        void Update(Fenrir::App& app, Fenrir::Scene& scene)
        {
            CamerasState* state = scene.GetResource<CamerasState>();
            if (state == nullptr)
            {
                return;
            }
            const float delta = static_cast<float>(app.GetTime().deltaTime);
            state->seconds += delta;

            // The spinning box, and the monitor camera circling the scene, looking at its middle.
            if (state->spinner.IsValid())
            {
                state->spinner.GetComponent<Fenrir::Transform>().rot =
                    Fenrir::Math::AngleAxis(state->seconds, Vec3(0.0f, 1.0f, 0.0f));
            }
            const float angle = state->seconds * 0.3f;
            auto& orbit = state->monitor.GetComponent<Fenrir::Transform>();
            orbit.pos = {7.0f * std::sin(angle), 3.0f, 7.0f * std::cos(angle)};
            orbit.rot = YawPitch(angle, -0.3f);

            const auto& keyboard = app.GetResource<Fenrir::Keyboard>();
            if (keyboard.JustPressed(Fenrir::Key::L))
            {
                state->layout =
                    static_cast<Layout>((static_cast<int>(state->layout) + 1) % static_cast<int>(Layout::Count));
                ApplyLayout(*state);
                constexpr std::array<const char*, 3> names{"one view", "side by side", "one over the other"};
                app.Logger()->Info("L: the main views are {}", names.at(static_cast<std::size_t>(state->layout)));
            }
            if (keyboard.JustPressed(Fenrir::Key::O))
            {
                state->minimapOver = !state->minimapOver;
                state->minimap.GetComponent<Fenrir::Camera>().order = state->minimapOver ? MinimapOver : MinimapUnder;
                app.Logger()->Info("O: the minimap's order is {}, so it draws {} the main views",
                                   state->minimapOver ? MinimapOver : MinimapUnder,
                                   state->minimapOver ? "over" : "under");
            }
            if (keyboard.JustPressed(Fenrir::Key::P))
            {
                state->orthographic = !state->orthographic;
                ApplyProjection(*state);
                app.Logger()->Info("P: the main views are {}", state->orthographic ? "orthographic" : "perspective");
            }
        }
    } // namespace

    FenrirDemo::DemoScene CamerasAndTargets()
    {
        FenrirDemo::DemoScene scene;
        scene.name = "Cameras and render targets";
        scene.keyHelp = "L: layout (one view, side by side, one over the other). O: the minimap's order, over or under "
                        "the main views. P: the main views' projection, perspective or orthographic.";
        scene.keys = {Fenrir::Key::L, Fenrir::Key::P, Fenrir::Key::O, Fenrir::Key::L,
                      Fenrir::Key::O, Fenrir::Key::P, Fenrir::Key::L};
        scene.spawn = Spawn;
        scene.update = Update;
        return scene;
    }
} // namespace RendererDemo
