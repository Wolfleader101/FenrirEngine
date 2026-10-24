#include "FenrirDemoScaffold/DemoScaffold.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirDebugDraw/DebugDrawing.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirScene/Scene.hpp"
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

            Fenrir::Entity freeFly;
            bool freeFlyOn = true;
            // The scene's cameras F3 turned off for the free-fly camera, turned back on by the next F3.
            std::vector<Fenrir::Entity> sceneCameras;
            float yaw = 0.0f;
            float pitch = 0.0f;
            float speed = BaseSpeed;

            bool helpShown = false;

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

            // Destroys the active demo scene's FenrirScene, leaving the holder scene active.
            void LeaveScene()
            {
                if (!current || app == nullptr)
                {
                    return;
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

                Log().Info("Scene {}/{}: {}. F1 for help.", index + 1, config.scenes.size(), demoScene.name);
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

            [[nodiscard]] std::vector<std::string> HelpLines() const
            {
                const DemoScene& scene = config.scenes[*current];
                std::vector<std::string> lines;
                lines.push_back(config.appName + ": " + scene.name + " (" + std::to_string(*current + 1) + "/" +
                                std::to_string(config.scenes.size()) + ")");
                lines.push_back("Scene keys: " + (scene.keyHelp.empty() ? std::string("none") : scene.keyHelp));
                if (!config.appKeyHelp.empty())
                {
                    lines.push_back("F2: " + config.appKeyHelp);
                }
                lines.emplace_back("PageDown/PageUp: next/previous scene. F1: this help. F3: free-fly camera on/off.");
                lines.emplace_back("Free-fly camera: hold the right mouse button to look, and with it held WASD to "
                                   "move, Shift to go faster and the wheel to change speed. Left click: scene click.");
                return lines;
            }

            void UpdateHelp(const Fenrir::Keyboard& keyboard)
            {
                if (keyboard.JustPressed(Fenrir::Key::F1))
                {
                    helpShown = !helpShown;
                    if (helpShown)
                    {
                        for (const std::string& line : HelpLines())
                        {
                            Log().Info("Help: {}", line);
                        }
                    }
                }
                if (!helpShown)
                {
                    return;
                }
                // Debug screen text, in the window's bottom-left corner, clear of the on-screen messages the renderer
                // stacks from its top-left.
                if (auto* draw = app->GetActiveScene().GetResource<Fenrir::DebugDraw>())
                {
                    constexpr float margin = 16.0f;
                    constexpr float lineHeight = 20.0f;
                    // Long lines wrap at a word, so the panel fits a window of the default size.
                    constexpr std::size_t widest = 100;
                    std::vector<std::string> lines;
                    for (std::string line : HelpLines())
                    {
                        while (line.size() > widest)
                        {
                            const std::size_t space = line.rfind(' ', widest);
                            // Past the indent of a wrapped line, or a word longer than a line is cut.
                            const bool atWord = space != std::string::npos && space > 2;
                            const std::size_t cut = atWord ? space : widest;
                            lines.push_back(line.substr(0, cut));
                            line.erase(0, atWord ? cut + 1 : cut);
                            line.insert(0, "  ");
                        }
                        lines.push_back(line);
                    }
                    const float height =
                        static_cast<float>(Fenrir::Window::LogicalSize(*app, Fenrir::Window::MainWindow()).height);
                    float y = std::max(margin, height - margin - lineHeight * static_cast<float>(lines.size()));
                    for (const std::string& line : lines)
                    {
                        draw->ScreenText({margin, y}, line, Fenrir::Colour::White);
                        y += lineHeight;
                    }
                }
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
                pitch = std::clamp(pitch - motion.y * LookPerUnit, -MaxPitch, MaxPitch);

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

            // BeforeTicks: the scene list, the help panel, the free-fly camera and the scene click.
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

                if (keyboard.JustPressed(Fenrir::Key::PageDown))
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
                UpdateHelp(keyboard);
                UpdateFreeFly(keyboard, mouse, static_cast<float>(app->GetTime().deltaTime));
                UpdateClick(mouse);
                if (Smoke())
                {
                    FeedSmokeKeys();
                }
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
            // ImGui turns text input on where it wants text, with its caret for the IME.
            window.textInputByDefault = !state->config.imguiOverlay;
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
