#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <utility>

#include <imgui.h>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirRenderer/SpawnModel.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirUI/UIRoot.hpp"
#include "UIDemo.hpp"

namespace UIDemo
{
    namespace
    {
        using Fenrir::Math::Vec3;

        constexpr float Pi = std::numbers::pi_v<float>;
        // The minimap's render target, square, and how many metres of the room its height spans.
        constexpr std::uint32_t MinimapPixels = 256;
        constexpr float MinimapMetres = 16.0f;
        // Stamina drains while the camera moves faster than this, in metres a second, and refills otherwise.
        constexpr float MovingSpeed = 0.5f;
        constexpr float StaminaDrain = 30.0f;
        constexpr float StaminaRefill = 20.0f;
        constexpr float HitDamage = 15.0f;
        // The menu's screen on the north wall: its width in metres, and how high its centre is.
        constexpr float ScreenMetres = 3.2f;
        constexpr float ScreenCentreHeight = 2.0f;

        /** @brief The room's own state, a scene resource. */
        struct RoomState
        {
            Fenrir::Entity player;
            Fenrir::Entity minimapCamera;
            Fenrir::AssetHandle<Fenrir::Image> minimap;
            std::optional<Vec3> lastCameraPosition;
            Fenrir::EventReader<FenrirDemo::DemoSceneClickEvent> clicks;
        };

        Fenrir::Math::Quat YawPitch(float yaw, float pitch)
        {
            return Fenrir::Math::AngleAxis(yaw, Vec3(0.0f, 1.0f, 0.0f)) *
                   Fenrir::Math::AngleAxis(pitch, Vec3(1.0f, 0.0f, 0.0f));
        }

        /** @brief A quad of `width` by `height` in the XY plane facing +Z, with UV (0, 0) at its top-left corner. */
        Fenrir::MeshData ScreenQuad(float width, float height)
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

        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            // The scene owns what it loads and makes, so its assets go when it is left.
            const auto cube = store.Load<Fenrir::Mesh>("cube.obj#Mesh0", scene);
            const auto sphere = store.Load<Fenrir::Mesh>("sphere.obj#Mesh0", scene);
            const auto crate = store.Load<Fenrir::Model>("BoxTextured/BoxTextured.gltf", scene);
            if (!cube.Ok() || !sphere.Ok() || !crate.Ok())
            {
                app.Logger()->Error("UIDemo: the cube, sphere or textured box did not load from the asset root");
                return;
            }
            const auto lit = store.Load<Fenrir::Shader>("builtin:lit", scene).handle;
            const auto material = [&](std::uint32_t srgb, float roughness) {
                const auto made = store.CreateMaterial(lit, scene);
                store.SetColour(made, "baseColour", Fenrir::Colour::FromSrgb(srgb));
                store.SetFloat(made, "roughness", roughness);
                return made;
            };

            Fenrir::EntityList& entities = scene.GetEntityList();
            const auto place = [&](Fenrir::AssetHandle<Fenrir::Mesh> mesh, Fenrir::AssetHandle<Fenrir::Material> paint,
                                   const Vec3& position, const Vec3& scale) {
                Fenrir::Entity entity = entities.CreateEntity();
                auto& transform = entity.AddComponent<Fenrir::Transform>();
                transform.pos = position;
                transform.scale = scale;
                entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{mesh, paint});
                return entity;
            };

            // A room of 14 by 14 metres with walls 4 high and no ceiling, so the top-down camera sees in.
            const auto floor = material(0x7A746CFF, 0.8f);
            const auto wall = material(0xB8B0A4FF, 0.9f);
            place(cube.handle, floor, {0.0f, -0.1f, 0.0f}, {14.0f, 0.2f, 14.0f});
            place(cube.handle, wall, {0.0f, 2.0f, -7.1f}, {14.4f, 4.0f, 0.2f});
            place(cube.handle, wall, {0.0f, 2.0f, 7.1f}, {14.4f, 4.0f, 0.2f});
            place(cube.handle, wall, {-7.1f, 2.0f, 0.0f}, {0.2f, 4.0f, 14.0f});
            place(cube.handle, wall, {7.1f, 2.0f, 0.0f}, {0.2f, 4.0f, 14.0f});

            // Pillars, crates and a few spheres to move round, so the compass and the minimap have something to show.
            const auto pillar = material(0x3C4656FF, 0.5f);
            for (const float x : {-3.5f, 3.5f})
            {
                for (const float z : {-3.5f, 3.5f})
                {
                    place(cube.handle, pillar, {x, 2.0f, z}, {0.6f, 4.0f, 0.6f});
                }
            }
            const std::array<Vec3, 4> crates{Vec3{-5.5f, 0.5f, -5.5f}, Vec3{-4.4f, 0.5f, -5.6f},
                                             Vec3{-5.0f, 1.5f, -5.5f}, Vec3{5.6f, 0.5f, 1.0f}};
            for (std::size_t i = 0; i < crates.size(); ++i)
            {
                Fenrir::Entity box = Fenrir::SpawnModel(app, scene, crate.handle);
                auto& transform = box.GetComponent<Fenrir::Transform>();
                transform.pos = crates.at(i);
                transform.rot = YawPitch(0.4f * static_cast<float>(i), 0.0f);
            }
            // The screen on the north wall, facing the camera's start: an unlit material showing the image the menu's
            // screen root draws into, in a dark bezel. The screen is 16 by 9, as its image is.
            if (const Menu* menu = app.TryGetResource<Menu>(); menu != nullptr)
            {
                const float height = ScreenMetres * static_cast<float>(ScreenHeight) / static_cast<float>(ScreenWidth);
                place(cube.handle, material(0x16181CFF, 0.4f), {0.0f, ScreenCentreHeight, -6.99f},
                      {ScreenMetres + 0.16f, height + 0.16f, 0.02f});
                const auto unlit = store.Load<Fenrir::Shader>("builtin:unlit", scene).handle;
                const auto screen = store.CreateMaterial(unlit, scene);
                store.SetTexture(screen, "baseColourTexture", menu->ScreenImage());
                const auto quad = store.Add<Fenrir::Mesh>(ScreenQuad(ScreenMetres, height), scene);
                place(quad, screen, {0.0f, ScreenCentreHeight, -6.975f}, Vec3(1.0f));
            }

            constexpr std::array<std::uint32_t, 3> ballColours{0xD04040FF, 0x40A060FF, 0x4070E0FF};
            for (std::size_t i = 0; i < ballColours.size(); ++i)
            {
                const float x = -1.5f + 1.5f * static_cast<float>(i);
                place(sphere.handle, material(ballColours.at(i), 0.3f), {x, 0.4f, 4.5f}, Vec3(0.8f));
            }

            // A dim sun through the open top and three warm lamps along the walls.
            Fenrir::Entity sun = entities.CreateEntity();
            sun.AddComponent<Fenrir::Transform>().rot = YawPitch(0.7f, -1.0f);
            sun.AddComponent<Fenrir::DirectionalLight>().intensity = 0.6f;
            const std::array<Vec3, 3> lamps{Vec3{-5.5f, 3.2f, 0.0f}, Vec3{5.5f, 3.2f, -4.0f}, Vec3{0.0f, 3.2f, 5.5f}};
            for (const Vec3& position : lamps)
            {
                Fenrir::Entity lamp = entities.CreateEntity();
                lamp.AddComponent<Fenrir::Transform>().pos = position;
                Fenrir::PointLight light;
                light.colour = Fenrir::Colour::FromSrgb(0xFFD8A8FF);
                light.intensity = 6.0f;
                light.range = 9.0f;
                lamp.AddComponent<Fenrir::PointLight>(light);
            }
            scene.AddResource<Fenrir::Sky>(Fenrir::ColourSky{Fenrir::Colour{0.05f, 0.06f, 0.08f, 1.0f}});

            RoomState state;
            // The player is the components the HUD shows; the free-fly camera is its eyes.
            state.player = entities.CreateEntity();
            state.player.AddComponent<Health>();
            state.player.AddComponent<Stamina>();
            state.player.AddComponent<Ammo>();

            // The minimap: an orthographic camera looking straight down, drawing into an image the HUD shows.
            Fenrir::ImageData target;
            target.width = MinimapPixels;
            target.height = MinimapPixels;
            state.minimap = store.Add<Fenrir::Image>(std::move(target), scene);
            Fenrir::Camera minimap;
            minimap.projection = Fenrir::Orthographic{MinimapMetres, 0.1f, 60.0f};
            minimap.target = Fenrir::ImageTarget{state.minimap};
            minimap.background = Fenrir::ColourBackground{Fenrir::Colour{0.02f, 0.03f, 0.05f, 1.0f}};
            state.minimapCamera = entities.CreateEntity();
            auto& minimapTransform = state.minimapCamera.AddComponent<Fenrir::Transform>();
            minimapTransform.pos = {0.0f, 30.0f, 0.0f};
            minimapTransform.rot = YawPitch(0.0f, -Pi * 0.5f);
            state.minimapCamera.AddComponent<Fenrir::Camera>(minimap);

            // Clicks made before the room are not its.
            (void)state.clicks.Read(app);
            scene.AddResource<RoomState>(std::move(state));
        }

        /** @brief The camera drawing into the main window: the enabled one of highest order. Null when none is. */
        Fenrir::Entity WindowCamera(Fenrir::Scene& scene)
        {
            Fenrir::EntityList& entities = scene.GetEntityList();
            const std::array<Fenrir::ComponentType, 2> cameras{Fenrir::ComponentType::Native<Fenrir::Camera>(),
                                                               Fenrir::ComponentType::Native<Fenrir::Transform>()};
            Fenrir::Entity found;
            std::int32_t order = 0;
            entities.ForEachWith(cameras, [&](std::uint32_t id) {
                Fenrir::Entity entity = entities.GetEntity(id);
                const auto& camera = entity.GetComponent<Fenrir::Camera>();
                if (camera.enabled && camera.target.Is<Fenrir::WindowTarget>() &&
                    (!found.IsValid() || camera.order > order))
                {
                    found = entity;
                    order = camera.order;
                }
            });
            return found;
        }

        /** @brief Degrees clockwise from north (-Z) that `rotation` looks along, seen from above. */
        float Heading(const Fenrir::Math::Quat& rotation)
        {
            const Vec3 forward = rotation * Vec3(0.0f, 0.0f, -1.0f);
            const float degrees = std::atan2(forward.x, -forward.z) * 180.0f / Pi;
            return degrees < 0.0f ? degrees + 360.0f : degrees;
        }

        /** @brief The game's side of the player: firing, reloading, taking hits and stamina. */
        void RunPlayer(Fenrir::App& app, RoomState& state, Hud& hud, float delta, float cameraSpeed)
        {
            const auto& keyboard = app.GetResource<Fenrir::Keyboard>();
            auto& ammo = state.player.GetComponent<Ammo>();
            auto& health = state.player.GetComponent<Health>();
            auto& stamina = state.player.GetComponent<Stamina>();

            // A left click on the room reaches the scene through the HUD, which takes no pointer input.
            std::int32_t shots = keyboard.JustPressed(Fenrir::Key::F) ? 1 : 0;
            for ([[maybe_unused]] const FenrirDemo::DemoSceneClickEvent& click : state.clicks.Read(app))
            {
                ++shots;
            }
            for (; shots > 0 && ammo.loaded > 0; --shots)
            {
                --ammo.loaded;
                hud.AddHitMarker();
            }
            if (keyboard.JustPressed(Fenrir::Key::R))
            {
                const std::int32_t taken = std::min(ammo.capacity - ammo.loaded, ammo.reserve);
                ammo.loaded += taken;
                ammo.reserve -= taken;
                // An empty reserve fills again, so the demo never runs dry.
                if (ammo.reserve == 0 && ammo.loaded == 0)
                {
                    ammo.reserve = 90;
                }
            }
            if (keyboard.JustPressed(Fenrir::Key::H))
            {
                health.current -= HitDamage;
                if (health.current <= 0.0f)
                {
                    health.current = health.max;
                }
            }
            const float rate = cameraSpeed > MovingSpeed ? -StaminaDrain : StaminaRefill;
            stamina.current = std::clamp(stamina.current + rate * delta, 0.0f, stamina.max);
        }

        /** @brief The switches, listed on screen in an ImGui window as LightingDemo lists its own. */
        void ShowSwitches(Hud& hud, const Menu& menu)
        {
            ImGui::SetNextWindowPos({12.0f, 12.0f}, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize({380.0f, 0.0f}, ImGuiCond_FirstUseEver);
            ImGui::Begin("UIDemo");
            ImGui::SeparatorText("Keys");
            ImGui::TextUnformatted("F or left click: fire (a hit marker, ammo down)");
            ImGui::TextUnformatted("R: reload");
            ImGui::TextUnformatted("H: take a hit (health down; refills at zero)");
            ImGui::TextUnformatted("Right mouse + WASD: move (stamina drains)");
            ImGui::TextUnformatted("Escape: the pause menu (B on a gamepad backs out)");
            ImGui::SeparatorText("Hot reload");
            bool hotReload = hud.Root().IsHotReloadOn();
            if (ImGui::Checkbox("Reload the HUD's files when they change", &hotReload))
            {
                hud.Root().SetHotReload(hotReload);
            }
            ImGui::TextUnformatted("Edit this stylesheet while the demo runs:");
            ImGui::TextWrapped("%s", hud.StylesheetPath().c_str());
            bool menuReload = menu.Root().IsHotReloadOn();
            if (ImGui::Checkbox("Reload the menu's files when they change", &menuReload))
            {
                menu.Root().SetHotReload(menuReload);
            }
            ImGui::TextUnformatted("The menu's stylesheet:");
            ImGui::TextWrapped("%s", menu.StylesheetPath().c_str());
            ImGui::SeparatorText("Settings");
            const Settings& settings = menu.GetSettings();
            // The engine has no audio yet: the volume is only kept and shown.
            ImGui::Text("Volume: %.0f (no audio yet)", settings.volume);
            ImGui::Text("Field of view: %.0f degrees", settings.fieldOfView);
            ImGui::End();
        }

        void Update(Fenrir::App& app, Fenrir::Scene& scene)
        {
            RoomState* state = scene.GetResource<RoomState>();
            Hud* hud = app.TryGetResource<Hud>();
            Menu* menu = app.TryGetResource<Menu>();
            if (state == nullptr || hud == nullptr || menu == nullptr || !state->player.IsValid())
            {
                return;
            }
            const float delta = static_cast<float>(app.GetTime().deltaTime);

            // The menu's system: Escape and the menu root's UI events.
            menu->Update();
            const Settings& settings = menu->GetSettings();
            if (auto* look = app.TryGetResource<FenrirDemo::FreeFlyLook>())
            {
                look->invertPitch = settings.invertLook;
            }

            float heading = 0.0f;
            float cameraSpeed = 0.0f;
            if (const Fenrir::Entity camera = WindowCamera(scene); camera.IsValid())
            {
                // The settings' field of view, on the camera drawing into the window.
                if (auto& lens = camera.GetComponent<Fenrir::Camera>().projection; lens.Is<Fenrir::Perspective>())
                {
                    lens.As<Fenrir::Perspective>().verticalFov = Fenrir::Math::DegToRad(settings.fieldOfView);
                }
                const auto& transform = camera.GetComponent<Fenrir::Transform>();
                heading = Heading(transform.rot);
                if (state->lastCameraPosition && delta > 0.0f)
                {
                    cameraSpeed = Fenrir::Math::Magnitude(transform.pos - *state->lastCameraPosition) / delta;
                }
                state->lastCameraPosition = transform.pos;
                // The minimap follows the camera across the room, north up.
                auto& above = state->minimapCamera.GetComponent<Fenrir::Transform>();
                above.pos = {transform.pos.x, above.pos.y, transform.pos.z};
            }

            // While the menu is open the game is paused: the room's keys do nothing.
            if (!menu->IsOpen())
            {
                RunPlayer(app, *state, *hud, delta, cameraSpeed);
            }

            // The HUD's system: what the player's components hold, pushed into the HUD's elements (ADR-0065).
            HudFrame frame;
            frame.health = state->player.GetComponent<Health>();
            frame.stamina = state->player.GetComponent<Stamina>();
            frame.ammo = state->player.GetComponent<Ammo>();
            frame.heading = heading;
            frame.minimap = state->minimap;
            frame.playerName = settings.playerName;
            frame.shown = settings.showHud;
            hud->Show(frame);
            hud->Update(delta);

            ShowSwitches(*hud, *menu);
        }
    } // namespace

    FenrirDemo::DemoScene Room()
    {
        FenrirDemo::DemoScene scene;
        scene.name = "The HUD";
        scene.keyHelp =
            "F or left click: fire. R: reload. H: take a hit. Right mouse and WASD: move, draining stamina. Escape: "
            "the pause menu.";
        // The smoke keys end by opening the menu and closing it again.
        scene.keys = {Fenrir::Key::F, Fenrir::Key::F, Fenrir::Key::H,      Fenrir::Key::F,
                      Fenrir::Key::R, Fenrir::Key::H, Fenrir::Key::Escape, Fenrir::Key::Escape};
        scene.camera = {{0.0f, 1.7f, 5.0f}, 0.0f, -0.1f};
        scene.spawn = Spawn;
        scene.update = Update;
        return scene;
    }
} // namespace UIDemo
