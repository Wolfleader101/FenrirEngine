#include "FenrirDemoScaffold/DemoScaffold.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirDebugDraw/DebugDrawing.hpp"
#include "FenrirDemoScaffold/DemoUI.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirUI/UI.hpp"
#include "FenrirWindow/Window.hpp"
#include "ImGuiOverlay.hpp"

#ifdef FENRIR_DEMO_SCRIPTING
#include "DemoScripting.hpp"
#include "FenrirScripting/Scripting.hpp"
#endif

namespace FenrirDemo
{
    namespace
    {
        // Free-fly camera tuning: metres a second, how much Shift multiplies it, radians per logical unit of mouse
        // motion, and how far each wheel notch scales the speed.
        constexpr float BaseSpeed = 4.0f;
        constexpr float FastFactor = 4.0f;
        constexpr float LookPerUnit = 0.0025f;
        constexpr float WheelFactor = 1.25f;
        constexpr float MinSpeed = 0.25f;
        constexpr float MaxSpeed = 200.0f;
        // Just short of straight up or down, where yaw stops meaning anything.
        constexpr float MaxPitch = 1.55f;

        // A smoke run's frames are this long, so it runs the same however fast the machine is.
        constexpr double SmokeFrameSeconds = 1.0 / 60.0;

        // The frame stats' frame time and fps are averaged over windows this long, so they can be read.
        constexpr double FrameStatsWindow = 0.5;

        /** @brief Counts the errors (and fatal messages) the app's logger is given, GL debug errors included. */
        class ErrorCounter : public Fenrir::ILogger
        {
          public:
            [[nodiscard]] std::size_t Count() const noexcept
            {
                return m_errors.load();
            }

            void AddLogger(std::shared_ptr<ILogger>) override
            {
            }
            void RemoveLogger(std::shared_ptr<ILogger>) override
            {
            }

          protected:
            void LogImpl(std::string_view) override
            {
            }
            void InfoImpl(std::string_view) override
            {
            }
            void WarnImpl(std::string_view) override
            {
            }
            // The OpenGL backend's debug callback may log from a driver thread.
            void ErrorImpl(std::string_view) override
            {
                ++m_errors;
            }
            void FatalImpl(std::string_view) override
            {
                ++m_errors;
            }

          private:
            std::atomic<std::size_t> m_errors{0};
        };

        /** @brief The value of the environment variable `name`, or empty when it is unset or empty. */
        std::string Environment(const char* name)
        {
#ifdef _MSC_VER
            char* value = nullptr;
            std::size_t length = 0;
            if (_dupenv_s(&value, &length, name) != 0 || value == nullptr)
            {
                return {};
            }
            std::string result(value);
            std::free(value);
            return result;
#else
            const char* value = std::getenv(name);
            return value == nullptr ? std::string() : std::string(value);
#endif
        }

        Fenrir::Scancode ScancodeOf(Fenrir::Key key)
        {
            // Key codes and scancodes are named alike: a smoke run presses keys as a US layout places them.
            return static_cast<Fenrir::Scancode>(static_cast<std::uint16_t>(key));
        }

        /**
         * @brief Where the demo UI kit's files are read from: the source tree's examples/DemoScaffold/assets while it
         * is there, so an edit there is what hot reload shows and a rebuild keeps, and the asset root otherwise.
         * FENRIR_DEMO_SOURCE_ASSETS is the scaffold's own definition.
         */
        std::filesystem::path DemoUIFolder(const Fenrir::AssetStore& store)
        {
#ifdef FENRIR_DEMO_SOURCE_ASSETS
            const std::filesystem::path source{FENRIR_DEMO_SOURCE_ASSETS};
            std::error_code error;
            if (std::filesystem::exists(source / "ui/demo.css", error))
            {
                return source;
            }
#endif
            return store.Root();
        }

        /**
         * @brief DemoConfig::frameStats's frame time and fps: averaged over windows of FrameStatsWindow seconds, and
         * over the frames so far until the first window is whole.
         */
        class FrameStats
        {
          public:
            /** @brief Adds a frame of `deltaTime` seconds. */
            void Count(double deltaTime)
            {
                if (deltaTime <= 0.0)
                {
                    return;
                }
                m_seconds += deltaTime;
                ++m_frames;
                const bool windowWhole = m_seconds >= FrameStatsWindow;
                if (windowWhole || !m_shownWhole)
                {
                    m_frameSeconds = m_seconds / m_frames;
                    m_framesPerSecond = m_frames / m_seconds;
                }
                if (windowWhole)
                {
                    m_shownWhole = true;
                    m_seconds = 0.0;
                    m_frames = 0;
                }
            }

            [[nodiscard]] double FrameSeconds() const noexcept
            {
                return m_frameSeconds;
            }
            [[nodiscard]] double FramesPerSecond() const noexcept
            {
                return m_framesPerSecond;
            }

          private:
            double m_seconds = 0.0;
            std::uint32_t m_frames = 0;
            bool m_shownWhole = false;
            double m_frameSeconds = 0.0;
            double m_framesPerSecond = 0.0;
        };

        /** @brief The demo UI kit as an app resource, removed when this owner ends: after the app's packages. */
        class DemoUIResource
        {
          public:
            DemoUIResource(Fenrir::App& app, DemoUIConfig config) : m_app(app)
            {
                app.AddResource<DemoUI>(app, std::move(config));
            }
            DemoUIResource(const DemoUIResource&) = delete;
            DemoUIResource& operator=(const DemoUIResource&) = delete;
            DemoUIResource(DemoUIResource&&) = delete;
            DemoUIResource& operator=(DemoUIResource&&) = delete;
            ~DemoUIResource()
            {
                if (m_app.TryGetResource<DemoUI>() != nullptr)
                {
                    m_app.RemoveResource<DemoUI>();
                }
            }

          private:
            Fenrir::App& m_app;
        };
    } // namespace

    DemoRunOptions DemoRunOptions::FromEnvironment(std::vector<std::string>* problems)
    {
        DemoRunOptions options;
        const auto problem = [problems](std::string text) {
            if (problems != nullptr)
            {
                problems->push_back(std::move(text));
            }
        };

        if (std::string scene = Environment("FENRIR_DEMO_SCENE"); !scene.empty())
        {
            options.scene = std::move(scene);
        }
        options.pressKeys = Environment("FENRIR_DEMO_KEYS") == "1";

        if (const std::string smoke = Environment("FENRIR_DEMO_SMOKE"); !smoke.empty())
        {
            // At most nine digits, so the number fits.
            const bool digits = smoke.size() <= 9 &&
                                std::all_of(smoke.begin(), smoke.end(), [](char c) { return c >= '0' && c <= '9'; });
            const auto frames = digits ? static_cast<std::uint32_t>(std::stoul(smoke)) : 0u;
            if (frames == 0)
            {
                problem("FENRIR_DEMO_SMOKE=" + smoke + " is not a positive number of frames; not a smoke run");
            }
            else
            {
                options.smokeFrames = frames;
            }
        }

        if (const std::string backend = Environment("FENRIR_DEMO_BACKEND"); backend == "null")
        {
            options.backend = Fenrir::GraphicsBackend::Null;
        }
        else if (!backend.empty() && backend != "opengl")
        {
            problem("FENRIR_DEMO_BACKEND=" + backend + " is neither null nor opengl; using opengl");
        }
        return options;
    }

    namespace
    {
        // Gives the main window the RGBA8 image at `path` as its icon (DemoConfig::iconPath). The pixels are copied, so
        // the image is unloaded again. A load that fails is logged by the store.
        void SetWindowIcon(Fenrir::App& app, Fenrir::AssetStore& store, const std::string& path)
        {
            const Fenrir::LoadResult<Fenrir::Image> loaded = store.Load<Fenrir::Image>(path, Fenrir::AssetOwner::App());
            if (!loaded.Ok())
            {
                return;
            }
            const Fenrir::ImageData* icon = store.TryGet(loaded.handle);
            if (icon == nullptr || icon->format != Fenrir::ImageFormat::RGBA8 ||
                icon->shape != Fenrir::ImageShape::Flat)
            {
                app.Logger()->Error("The window icon {} is not one RGBA8 picture (a PNG or JPEG)", path);
            }
            else
            {
                Fenrir::Window::SetIcon(app, Fenrir::Window::MainWindow(), static_cast<int>(icon->width),
                                        static_cast<int>(icon->height), icon->pixels);
            }
            store.Unload(loaded.handle);
        }
    } // namespace

    namespace Detail
    {
        struct DemoState
        {
            Fenrir::App* app = nullptr;
            DemoConfig config;
            DemoRunOptions run;
            std::shared_ptr<ErrorCounter> errors = std::make_shared<ErrorCounter>();

            // In install order, so they are destroyed in reverse: the renderer first, the window last.
            std::optional<Fenrir::Window> window;
            std::optional<Fenrir::Input> input;
            std::optional<Fenrir::Assets> assets;
            std::optional<Fenrir::DebugDrawing> debugDrawing;
            std::optional<Fenrir::Renderer> renderer;
            std::optional<Fenrir::UI> ui;
            // The demo UI kit, over FenrirUI.
            std::optional<DemoUIResource> kit;
            // DemoConfig::installPackages's owners: after the scaffold's packages and the kit, which they need, and
            // before Scripting.
            std::shared_ptr<void> appPackages;
#ifdef FENRIR_DEMO_SCRIPTING
            // Last, so it ends first, while the packages its extensions reach are there.
            std::optional<Fenrir::Scripting> scripting;
#endif
            // DemoConfig::imguiOverlay: ended first, while the input and the asset store it uses are there.
            std::optional<ImGuiOverlay> imgui;

            // The App's own first scene, kept empty and active between leaving one demo scene and entering the next.
            std::string holderScene;
            bool started = false;
            std::optional<std::size_t> current;
            // Scene entries so far, which a C# scene's CSharpScene marker carries.
            std::uint32_t entries = 0;
            // The scenes entered so far in the run, by index: a scene's tutorial shows on its first entry only.
            std::vector<bool> entered;
            // The scene a click on the scene switcher picked, entered at the next BeforeTicks.
            std::optional<std::size_t> picked;

            // DemoConfig::frameStats's frame time and fps.
            FrameStats frameStats;

            Fenrir::Entity freeFly;
            bool freeFlyOn = true;
            // The scene's cameras F3 turned off for the free-fly camera, turned back on by the next F3.
            std::vector<Fenrir::Entity> sceneCameras;
            float yaw = 0.0f;
            float pitch = 0.0f;
            float speed = BaseSpeed;

            // Smoke runs: the scenes to run in order, the one running, and its frames so far.
            std::vector<std::size_t> smokeScenes;
            std::size_t smokeNext = 0;
            std::uint32_t framesInScene = 0;
            std::vector<Fenrir::Key> releaseNextFrame;

            // FENRIR_DEMO_KEYS: simulation seconds in the scene and keys pressed so far.
            double secondsActive = 0.0;
            std::size_t keysPressed = 0;

            DemoState() = default;
            DemoState(const DemoState&) = delete;
            DemoState& operator=(const DemoState&) = delete;

            ~DemoState()
            {
                // The demo scene goes before the packages, so its assets and entities end while they are there.
                LeaveScene();
                if (app != nullptr)
                {
                    app->Logger()->RemoveLogger(errors);
                }
            }

            [[nodiscard]] Fenrir::ILogger& Log() const
            {
                return *app->Logger();
            }

            [[nodiscard]] bool Smoke() const
            {
                return run.smokeFrames.has_value();
            }

            [[nodiscard]] std::optional<std::size_t> Find(std::string_view name) const
            {
                for (std::size_t i = 0; i < config.scenes.size(); ++i)
                {
                    if (config.scenes[i].name == name)
                    {
                        return i;
                    }
                }
                return std::nullopt;
            }

            // The first frame: picks the scenes a smoke run runs, or the scene to start in, and enters it.
            void Start()
            {
                started = true;
                if (config.scenes.empty())
                {
                    Log().Warn("{} has no scenes", config.appName);
                    if (Smoke())
                    {
                        app->Stop();
                    }
                    return;
                }

                std::optional<std::size_t> named;
                if (run.scene)
                {
                    named = Find(*run.scene);
                    if (!named)
                    {
                        Log().Error("FENRIR_DEMO_SCENE: {} has no scene \"{}\"", config.appName, *run.scene);
                    }
                }

                if (Smoke())
                {
                    if (named)
                    {
                        smokeScenes = {*named};
                    }
                    else
                    {
                        for (std::size_t i = 0; i < config.scenes.size(); ++i)
                        {
                            smokeScenes.push_back(i);
                        }
                    }
                    Log().Info("FENRIR_DEMO_SMOKE: running {} scene(s) of {} for {} frames each", smokeScenes.size(),
                               config.appName, *run.smokeFrames);
                    EnterScene(smokeScenes[smokeNext++]);
                    return;
                }
                EnterScene(named.value_or(0));
            }

            /** @brief The demo UI kit; null when FenrirUI did not install. */
            [[nodiscard]] DemoUI* Kit() const
            {
                return kit ? app->TryGetResource<DemoUI>() : nullptr;
            }

            // Destroys the active demo scene's FenrirScene, leaving the holder scene active.
            void LeaveScene()
            {
                if (!current || app == nullptr)
                {
                    return;
                }
                // Its rows first, while the state they are bound to is there.
                if (DemoUI* demoUI = Kit())
                {
                    demoUI->ClearScene();
                }
                app->ChangeActiveScene(holderScene);
                app->DestroyScene(config.scenes[*current].name);
                current.reset();
                freeFly = Fenrir::Entity();
                sceneCameras.clear();
            }

            void EnterScene(std::size_t index)
            {
                LeaveScene();
                const DemoScene& demoScene = config.scenes[index];
                Fenrir::Scene& scene = app->CreateScene(demoScene.name);
                app->ChangeActiveScene(demoScene.name);
                current = index;
                framesInScene = 0;
                secondsActive = 0.0;
                keysPressed = 0;
                ++entries;
                if (demoScene.csharpScene != 0)
                {
                    scene.AddResource<CSharpScene>(CSharpScene{demoScene.csharpScene, entries});
                }

                // The scene's Tutorial button first, so its section comes before the scene's own.
                if (DemoUI* demoUI = Kit(); demoUI != nullptr && demoScene.tutorial)
                {
                    demoUI->SceneSection(demoScene.name)
                        .Button(
                            "Tutorial", [demoUI, &demoScene] { demoUI->ShowTutorial(*demoScene.tutorial); },
                            "Shows the scene's tutorial again from its first step");
                }

                // The free-fly camera first, so the scene can find it.
                Fenrir::EntityList& entities = scene.GetEntityList();
                freeFly = entities.CreateEntity();
                freeFly.AddComponent<Fenrir::Transform>();
                freeFly.AddComponent<Fenrir::Camera>();
                yaw = demoScene.camera.yaw;
                pitch = demoScene.camera.pitch;
                speed = BaseSpeed;
                PlaceFreeFly(demoScene.camera.position);

                if (demoScene.spawn)
                {
                    demoScene.spawn(*app, scene);
                }

                // A scene with a camera of its own draws through it; F3 switches to the free-fly camera.
                freeFlyOn = !AnySceneCamera(scene);
                freeFly.GetComponent<Fenrir::Camera>().enabled = freeFlyOn;

                Log().Info("Scene {}/{}: {}. F1 hides or shows the demo UI kit.", index + 1, config.scenes.size(),
                           demoScene.name);
                if (DemoUI* demoUI = Kit())
                {
                    demoUI->SetActiveScene(index);
                    demoUI->SetHelp(config.appName + ": " + demoScene.name + " (" + std::to_string(index + 1) + "/" +
                                        std::to_string(config.scenes.size()) + ")",
                                    HelpRows(demoScene));
                }
                // A run that presses keys by itself is watched by no one, so it shows no tutorial.
                entered.resize(config.scenes.size());
                const bool first = !entered[index];
                entered[index] = true;
                if (DemoUI* demoUI = Kit();
                    demoUI != nullptr && demoScene.tutorial && first && !Smoke() && !run.pressKeys)
                {
                    demoUI->ShowTutorial(*demoScene.tutorial);
                }
            }

            [[nodiscard]] bool AnySceneCamera(Fenrir::Scene& scene) const
            {
                bool found = false;
                ForEachSceneCamera(scene,
                                   [&](Fenrir::Entity, Fenrir::Camera& camera) { found = found || camera.enabled; });
                return found;
            }

            // Calls `func(entity, camera)` for every camera in `scene` that draws into a window, but the free-fly
            // camera. Cameras that draw into images (a monitor's) are left alone: the free-fly camera stands in only
            // for the views of the window, and what an image camera draws must keep changing.
            template <typename Func>
            void ForEachSceneCamera(Fenrir::Scene& scene, Func&& func) const
            {
                Fenrir::EntityList& entities = scene.GetEntityList();
                const std::array<Fenrir::ComponentType, 1> cameras{Fenrir::ComponentType::Native<Fenrir::Camera>()};
                entities.ForEachWith(cameras, [&](std::uint32_t id) {
                    if (id == freeFly.GetId())
                    {
                        return;
                    }
                    const Fenrir::Entity entity = entities.GetEntity(id);
                    Fenrir::Camera& camera = entity.GetComponent<Fenrir::Camera>();
                    if (camera.target.Is<Fenrir::WindowTarget>())
                    {
                        func(entity, camera);
                    }
                });
            }

            [[nodiscard]] Fenrir::Math::Quat FreeFlyRotation() const
            {
                return Fenrir::Math::AngleAxis(yaw, Fenrir::Math::Vec3(0.0f, 1.0f, 0.0f)) *
                       Fenrir::Math::AngleAxis(pitch, Fenrir::Math::Vec3(1.0f, 0.0f, 0.0f));
            }

            void PlaceFreeFly(const Fenrir::Math::Vec3& position)
            {
                auto& transform = freeFly.GetComponent<Fenrir::Transform>();
                transform.pos = position;
                transform.rot = FreeFlyRotation();
            }

            void Step(int direction)
            {
                const auto count = static_cast<std::ptrdiff_t>(config.scenes.size());
                const auto from = static_cast<std::ptrdiff_t>(current.value_or(0));
                const std::ptrdiff_t to = ((from + direction) % count + count) % count;
                EnterScene(static_cast<std::size_t>(to));
            }

            void ToggleFreeFly()
            {
                if (!current)
                {
                    return;
                }
                Fenrir::Scene& scene = app->GetActiveScene();
                freeFlyOn = !freeFlyOn;
                freeFly.GetComponent<Fenrir::Camera>().enabled = freeFlyOn;
                if (freeFlyOn)
                {
                    ForEachSceneCamera(scene, [&](Fenrir::Entity entity, Fenrir::Camera& camera) {
                        if (camera.enabled)
                        {
                            camera.enabled = false;
                            sceneCameras.push_back(entity);
                        }
                    });
                }
                else
                {
                    for (const Fenrir::Entity& entity : sceneCameras)
                    {
                        if (entity.IsValid() && entity.HasComponent<Fenrir::Camera>())
                        {
                            entity.GetComponent<Fenrir::Camera>().enabled = true;
                        }
                    }
                    sceneCameras.clear();
                }
                Log().Info("F3: the free-fly camera is {}", freeFlyOn ? "on" : "off");
            }

            // The help's key rows: the scaffold's, the app's, then the scene's. An app or a scene with only its one
            // line of key help shows it as one row.
            [[nodiscard]] std::vector<DemoKeyRow> HelpRows(const DemoScene& scene) const
            {
                std::vector<DemoKeyRow> rows{
                    {"PageDown/PageUp", "next/previous scene"},
                    {"F1", "hide or show the demo UI kit"},
                    {"F3", "free-fly camera on/off"},
                    {"Right mouse", "hold to look with the free-fly camera"},
                    {"Right mouse + WASD", "fly; Shift goes faster, the wheel changes the speed"},
                    {"Left click", "scene click"},
                };
                if (!config.appKeyRows.empty())
                {
                    rows.insert(rows.end(), config.appKeyRows.begin(), config.appKeyRows.end());
                }
                else if (!config.appKeyHelp.empty())
                {
                    rows.push_back({"F2", config.appKeyHelp});
                }
                if (!scene.keyRows.empty())
                {
                    rows.insert(rows.end(), scene.keyRows.begin(), scene.keyRows.end());
                }
                else if (!scene.keyHelp.empty())
                {
                    rows.push_back({"", scene.keyHelp});
                }
                return rows;
            }

            void UpdateFreeFly(const Fenrir::Keyboard& keyboard, const Fenrir::Mouse& mouse, float deltaTime)
            {
                if (!freeFlyOn || !freeFly.IsValid())
                {
                    return;
                }
                if (mouse.JustPressed(Fenrir::MouseButton::Right))
                {
                    Fenrir::Window::SetCursorMode(*app, Fenrir::Window::MainWindow(), Fenrir::CursorMode::Captured);
                }
                if (mouse.JustReleased(Fenrir::MouseButton::Right))
                {
                    Fenrir::Window::SetCursorMode(*app, Fenrir::Window::MainWindow(), Fenrir::CursorMode::Normal);
                }
                // WASD and the wheel are the scene's unless the right mouse button is held.
                if (!mouse.IsDown(Fenrir::MouseButton::Right))
                {
                    return;
                }

                const Fenrir::Math::Vec2 motion = mouse.Motion();
                yaw -= motion.x * LookPerUnit;
                const FreeFlyLook* look = app->TryGetResource<FreeFlyLook>();
                const float pitchMotion = look != nullptr && look->invertPitch ? -motion.y : motion.y;
                pitch = std::clamp(pitch - pitchMotion * LookPerUnit, -MaxPitch, MaxPitch);

                const float notches = mouse.Scroll().y;
                if (notches != 0.0f)
                {
                    speed = std::clamp(speed * std::pow(WheelFactor, notches), MinSpeed, MaxSpeed);
                }

                const Fenrir::Math::Quat rotation = FreeFlyRotation();
                const Fenrir::Math::Vec3 forward = rotation * Fenrir::Math::Vec3(0.0f, 0.0f, -1.0f);
                const Fenrir::Math::Vec3 right = rotation * Fenrir::Math::Vec3(1.0f, 0.0f, 0.0f);
                Fenrir::Math::Vec3 move(0.0f, 0.0f, 0.0f);
                move += keyboard.IsDown(Fenrir::Scancode::W) ? forward : Fenrir::Math::Vec3(0.0f);
                move -= keyboard.IsDown(Fenrir::Scancode::S) ? forward : Fenrir::Math::Vec3(0.0f);
                move += keyboard.IsDown(Fenrir::Scancode::D) ? right : Fenrir::Math::Vec3(0.0f);
                move -= keyboard.IsDown(Fenrir::Scancode::A) ? right : Fenrir::Math::Vec3(0.0f);
                const bool fast =
                    keyboard.IsDown(Fenrir::Scancode::LeftShift) || keyboard.IsDown(Fenrir::Scancode::RightShift);

                auto& transform = freeFly.GetComponent<Fenrir::Transform>();
                transform.pos += move * (speed * (fast ? FastFactor : 1.0f) * deltaTime);
                transform.rot = rotation;
            }

            // The ray from the camera drawn at the clicked point, through it (ADR-0026's picking).
            void UpdateClick(const Fenrir::Mouse& mouse)
            {
                if (!mouse.JustPressed(Fenrir::MouseButton::Left) || mouse.IsDown(Fenrir::MouseButton::Right) ||
                    !renderer || !renderer->IsValid())
                {
                    return;
                }
                const Fenrir::Math::Vec2 position = mouse.Position();
                const Fenrir::Entity camera = renderer->CameraAt(Fenrir::Window::MainWindow(), position);
                if (const std::optional<Fenrir::Math::Ray> ray = renderer->ScreenPointToRay(camera, position))
                {
                    app->SendEvent(DemoSceneClickEvent{*ray, position});
                }
            }

            // A smoke run's keys: key i of n goes down at frame (i + 1) * frames / (n + 1), and up a frame later.
            void FeedSmokeKeys()
            {
                auto& feed = app->GetResource<Fenrir::InputFeed>();
                for (const Fenrir::Key key : releaseNextFrame)
                {
                    feed.KeyUp(Fenrir::Window::MainWindow(), key, ScancodeOf(key));
                }
                releaseNextFrame.clear();

                const std::vector<Fenrir::Key>& keys = config.scenes[*current].keys;
                const std::uint32_t frames = *run.smokeFrames;
                for (std::size_t i = 0; i < keys.size(); ++i)
                {
                    const auto at = static_cast<std::uint32_t>((i + 1) * frames / (keys.size() + 1));
                    if (at == framesInScene)
                    {
                        feed.KeyDown(Fenrir::Window::MainWindow(), keys[i], ScancodeOf(keys[i]));
                        releaseNextFrame.push_back(keys[i]);
                    }
                }
            }

            // FENRIR_DEMO_KEYS: two seconds after entry, to let the scene settle, then one key a second; the app stops
            // three seconds after the last, for its effects to log.
            void PressKeysInTime(double deltaTime)
            {
                auto& feed = app->GetResource<Fenrir::InputFeed>();
                for (const Fenrir::Key key : releaseNextFrame)
                {
                    feed.KeyUp(Fenrir::Window::MainWindow(), key, ScancodeOf(key));
                }
                releaseNextFrame.clear();

                const DemoScene& scene = config.scenes[*current];
                // A long frame counts as a short one: the first frames' deltas include loading, which is not settling.
                secondsActive += std::min(deltaTime, 0.1);
                const std::size_t count = scene.keys.size();
                if (keysPressed >= count)
                {
                    if (secondsActive >= 2.0 + static_cast<double>(count) + 3.0)
                    {
                        Log().Info("FENRIR_DEMO_KEYS: pressed all {} keys in {}; stopping.", count, scene.name);
                        app->Stop();
                    }
                    return;
                }
                if (secondsActive < 2.0 + static_cast<double>(keysPressed))
                {
                    return;
                }
                const Fenrir::Key key = scene.keys[keysPressed++];
                Log().Info("FENRIR_DEMO_KEYS: pressing key {} ({}/{}) in {}.", static_cast<int>(key), keysPressed,
                           count, scene.name);
                feed.KeyDown(Fenrir::Window::MainWindow(), key, ScancodeOf(key));
                releaseNextFrame.push_back(key);
            }

            // BeforeTicks: the scene list, the free-fly camera and the scene click. F1 is the demo UI kit's own.
            void BeforeTicks()
            {
                // Before anything makes an ImGui window this frame.
                if (imgui)
                {
                    imgui->BeginFrame();
                }
                if (!started)
                {
                    Start();
                }
                if (!current)
                {
                    return;
                }
                const auto& keyboard = app->GetResource<Fenrir::Keyboard>();
                const auto& mouse = app->GetResource<Fenrir::Mouse>();

                if (config.frameStats)
                {
                    frameStats.Count(app->GetTime().deltaTime);
                }
                if (picked)
                {
                    // Entered as PageDown enters a scene, and in a smoke run as well.
                    const std::size_t index = *picked;
                    picked.reset();
                    EnterScene(index);
                }
                else if (keyboard.JustPressed(Fenrir::Key::PageDown))
                {
                    Step(1);
                }
                else if (keyboard.JustPressed(Fenrir::Key::PageUp))
                {
                    Step(-1);
                }
                if (keyboard.JustPressed(Fenrir::Key::F3))
                {
                    ToggleFreeFly();
                }
                UpdateFreeFly(keyboard, mouse, static_cast<float>(app->GetTime().deltaTime));
                UpdateClick(mouse);
                if (Smoke())
                {
                    FeedSmokeKeys();
                }
            }

            // The kit's app sections the scaffold feeds: the scene switcher first, then the frame stats.
            void AddAppSections(DemoUI& demoUI, const std::weak_ptr<DemoState>& weak)
            {
                std::vector<std::string> names;
                names.reserve(config.scenes.size());
                for (const DemoScene& scene : config.scenes)
                {
                    names.push_back(scene.name);
                }
                demoUI.SetScenes(std::move(names), [weak](std::size_t index) {
                    // The active scene's button leaves it be: PageDown would leave it.
                    if (const auto locked = weak.lock(); locked && locked->current != index)
                    {
                        locked->picked = index;
                    }
                });
                if (!config.frameStats)
                {
                    return;
                }
                // Pulled in the kit's build stage work, after the scenes' Update: the renderer's stats are the last
                // frame's, which is the one the window shows.
                const DemoSection frame = demoUI.AppSection("Frame");
                const auto pulled = [weak](auto read) {
                    return [weak, read] {
                        const auto locked = weak.lock();
                        return locked ? read(*locked) : std::string();
                    };
                };
                frame.Readout("Frame time", pulled([](const DemoState& state) {
                                  return std::format("{:.1f} ms", state.frameStats.FrameSeconds() * 1000.0);
                              }));
                frame.Readout("FPS", pulled([](const DemoState& state) {
                                  return std::format("{:.0f}", state.frameStats.FramesPerSecond());
                              }));
                const auto stat = [&](std::string label, std::uint32_t Fenrir::RendererStats::* field) {
                    frame.Readout(std::move(label), pulled([field](const DemoState& state) {
                                      return state.renderer && state.renderer->IsValid()
                                                 ? std::to_string(state.renderer->Stats().*field)
                                                 : std::string("-");
                                  }));
                };
                stat("Draws", &Fenrir::RendererStats::draws);
                stat("Instances", &Fenrir::RendererStats::instances);
                stat("Shadow draws", &Fenrir::RendererStats::shadowDraws);
            }

            // Update: the scene's own update, then a smoke run's or FENRIR_DEMO_KEYS's bookkeeping.
            void Update()
            {
                if (!current)
                {
                    return;
                }
                const DemoScene& scene = config.scenes[*current];
                if (scene.update)
                {
                    scene.update(*app, app->GetActiveScene());
                }

                if (Smoke())
                {
                    if (++framesInScene < *run.smokeFrames)
                    {
                        return;
                    }
                    Log().Info("FENRIR_DEMO_SMOKE: ran {} frames of {}", framesInScene, scene.name);
                    if (smokeNext < smokeScenes.size())
                    {
                        EnterScene(smokeScenes[smokeNext++]);
                    }
                    else
                    {
                        app->Stop();
                    }
                    return;
                }
                if (run.pressKeys)
                {
                    PressKeysInTime(app->GetTime().deltaTime);
                }
            }
        };
    } // namespace Detail

    DemoScaffold DemoScaffold::Install(Fenrir::App& app, DemoConfig config)
    {
        std::vector<std::string> problems;
        DemoRunOptions run = config.run ? *config.run : DemoRunOptions::FromEnvironment(&problems);

        auto state = std::make_shared<Detail::DemoState>();
        state->app = &app;
        state->run = run;
        state->config = std::move(config);
        state->holderScene = app.GetActiveScene().GetName();
        if (app.TryGetResource<FreeFlyLook>() == nullptr)
        {
            app.AddResource<FreeFlyLook>();
        }
        app.Logger()->AddLogger(state->errors);
        for (const std::string& problem : problems)
        {
            app.Logger()->Warn("{}", problem);
        }

        const bool smoke = run.smokeFrames.has_value();
        if (run.backend != Fenrir::GraphicsBackend::Null)
        {
            Fenrir::WindowConfig window;
            window.title = state->config.appName;
            // A smoke run is a check, not a show: its window is never shown.
            window.visible = !smoke;
            // The demo UI kit, and ImGui while it is there, turn text input on where they want text, with their caret
            // for the IME.
            window.textInputByDefault = false;
            state->window = Fenrir::Window::Install(app, window);
        }
        state->input = Fenrir::Input::Install(app);
        state->assets = Fenrir::Assets::Install(app);
        state->debugDrawing = Fenrir::DebugDrawing::Install(app);
        if (state->window && state->window->IsValid() && !state->config.iconPath.empty())
        {
            SetWindowIcon(app, state->assets->Store(), state->config.iconPath);
        }

        Fenrir::RendererConfig renderer;
        renderer.backend = run.backend;
        renderer.msaa = state->config.msaa;
        if (smoke)
        {
            // GL debug errors fail a smoke run in every build, and it runs as fast as it can.
            renderer.debugValidation = true;
            renderer.vsync = Fenrir::Vsync::Off;
        }
        state->renderer = Fenrir::Renderer::Install(app, renderer);
        // FenrirUI for every demo app, and the kit over it, before the app's packages, whose roots may need it.
        state->ui = Fenrir::UI::Install(app);
        if (state->ui->IsValid())
        {
            DemoUIConfig kit;
            kit.folder = DemoUIFolder(state->assets->Store());
            // A smoke run is a check of the files as built, not an editing session.
            kit.hotReload = !smoke;
            kit.target = state->config.demoUITarget;
            state->kit.emplace(app, std::move(kit));
            app.GetResource<DemoUI>().SetTitle(state->config.appName);
        }
        // Before the app's packages, so the switcher and the frame stats are the panel's first app sections.
        if (DemoUI* demoUI = state->Kit())
        {
            state->AddAppSections(*demoUI, state);
        }
        if (state->config.installPackages)
        {
            state->appPackages = state->config.installPackages(app);
        }
#ifdef FENRIR_DEMO_SCRIPTING
        if (!state->config.scriptAssembly.empty())
        {
            Fenrir::ScriptingConfig scripting;
            // The asset store's root is the default, `assets` beside the executable, so its parent is the
            // executable's directory, where fenrir_add_demo_app puts the scripts.
            scripting.scriptAssembly = state->assets->Store().Root().parent_path() / state->config.scriptAssembly;
            // A smoke run is a check of the scripts as built, not an editing session.
            scripting.hotReload = !smoke;
            scripting.extensions = Detail::DemoScriptingExtensions();
            // The app's own, after the ones they may build on.
            std::ranges::move(state->config.scriptingExtensions, std::back_inserter(scripting.extensions));
            state->scripting = Fenrir::Scripting::Install(app, std::move(scripting));
        }
#endif

        if (state->config.imguiOverlay)
        {
            state->imgui.emplace(app, state->assets->Store());
        }

        std::weak_ptr<Detail::DemoState> weak = state;
        app.AddSequentialSystem(Fenrir::SchedulePriority::BeforeTicks, [weak](Fenrir::App&) {
            if (const auto locked = weak.lock())
            {
                locked->BeforeTicks();
            }
        });
        app.AddSequentialSystem(Fenrir::SchedulePriority::Update, [weak](Fenrir::App&) {
            if (const auto locked = weak.lock())
            {
                locked->Update();
            }
        });
        if (state->imgui)
        {
            // After every scene's Update: what ImGui drew this frame goes to the overlay before Render.
            app.AddSequentialSystem(Fenrir::SchedulePriority::PostUpdate, [weak](Fenrir::App&) {
                if (const auto locked = weak.lock(); locked && locked->imgui)
                {
                    locked->imgui->EndFrame();
                }
            });
        }
        return DemoScaffold(std::move(state));
    }

    DemoScaffold::DemoScaffold(std::shared_ptr<Detail::DemoState> state) : m_state(std::move(state))
    {
    }

    DemoScaffold::DemoScaffold(DemoScaffold&& other) noexcept = default;
    DemoScaffold& DemoScaffold::operator=(DemoScaffold&& other) noexcept = default;
    DemoScaffold::~DemoScaffold() = default;

    bool DemoScaffold::RendererStarted() const
    {
        return m_state != nullptr && m_state->renderer && m_state->renderer->IsValid();
    }

    int DemoScaffold::Run()
    {
        if (m_state == nullptr)
        {
            return 1;
        }
        Detail::DemoState& state = *m_state;
        Fenrir::App& app = *state.app;
        const bool smoke = state.Smoke();
        if (!RendererStarted())
        {
            if (smoke)
            {
                app.Logger()->Info("FENRIR_DEMO_SMOKE: the renderer could not start, so the smoke run is skipped");
                return SkippedExitCode;
            }
            return 1;
        }

        if (!smoke)
        {
            app.Run();
            return 0;
        }
        while (app.IsRunning())
        {
            app.RunFrame(SmokeFrameSeconds);
        }
        app.RunExit();
        const std::size_t errors = state.errors->Count();
        if (errors > 0)
        {
            app.Logger()->Info("FENRIR_DEMO_SMOKE: failed, with {} error(s) logged", errors);
            return 1;
        }
        app.Logger()->Info("FENRIR_DEMO_SMOKE: passed");
        return 0;
    }

    Fenrir::Entity DemoScaffold::FreeFlyCamera() const
    {
        return m_state != nullptr ? m_state->freeFly : Fenrir::Entity();
    }

    bool DemoScaffold::FreeFlyCameraOn() const
    {
        return m_state != nullptr && m_state->freeFlyOn;
    }
} // namespace FenrirDemo
