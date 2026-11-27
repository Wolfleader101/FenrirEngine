#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirAssets/MeshData.hpp"
#include "FenrirDemoScaffold/DemoUI.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirInput/InputTypes.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirRenderer/Renderer.hpp"

#ifdef FENRIR_DEMO_SCRIPTING
#include "FenrirScripting/ScriptingExtension.hpp"
#endif

// The demo scaffold (GLOSSARY.md): what every demo app is built on. DemoScaffold::Install installs the engine packages
// a demo app needs, FenrirUI among them, and adds the demo UI kit (DemoUI.hpp), the free-fly camera, the scene list,
// the scene-click event, the help and the smoke-run switches. Demo code, not engine code: the engine knows scenes, not
// demo scenes.

namespace Fenrir
{
    class App;
    class Scene;
    class UITarget;
} // namespace Fenrir

namespace FenrirDemo
{
    namespace Detail
    {
        /** @brief What DemoScaffold::Install set up. Defined in the scaffold's sources. */
        struct DemoState;
    } // namespace Detail

    /** @brief Where the free-fly camera starts in a scene: a position, and the yaw and pitch it looks along. */
    struct DemoCameraStart
    {
        Fenrir::Math::Vec3 position{0.0f, 1.0f, 5.0f};
        /** @brief Radians about +Y; 0 looks along -Z. */
        float yaw = 0.0f;
        /** @brief Radians about the camera's +X; positive looks up. */
        float pitch = 0.0f;
    };

    /**
     * @brief One entry of a demo app's ordered scene list. Each time it is entered it is spawned into a FenrirScene of
     * its own, named after it, which is destroyed when it is left, with the assets loaded for it.
     */
    struct DemoScene
    {
        /** @brief The scene's name, unique in the app: shown in the help, and what FENRIR_DEMO_SCENE names. */
        std::string name;
        /** @brief The scene's key rows, the help's last. */
        std::vector<DemoKeyRow> keyRows;
        /**
         * @brief The scene's demo tutorial: shown the first time the scene is entered in a run, but never in a smoke
         * run or with FENRIR_DEMO_KEYS, and again from the Tutorial button in the scene's section, titled after the
         * scene, which the scaffold adds before `spawn` adds the scene's own.
         */
        std::optional<DemoTutorial> tutorial;
        /**
         * @brief The id of the scene's demo slot (GLOSSARY.md), for a scene with a Razor panel: an element of this id
         * in the demo panel (DemoUI::SceneSlot), made when the scene is entered, after its Tutorial section and before
         * `spawn` adds its own sections, and gone when it is left. The app's C# finds it by this id
         * (`DemoUI.TryGetSceneSlot` in the Fenrir.Demo assembly) and mounts its Razor panel into it. Without scripting
         * the slot shows a note that the panel needs it, so every switch on the panel needs a key in `keyRows`
         * (ADR-0072). Unique in the app; empty for none.
         */
        std::string razorPanel;
        /** @brief The keys a smoke run or FENRIR_DEMO_KEYS presses in this scene, in order. */
        std::vector<Fenrir::Key> keys;
        /** @brief Where the free-fly camera starts. */
        DemoCameraStart camera;
        /**
         * @brief Spawns the scene into `scene`, the active scene, which already holds the free-fly camera. A scene that
         * spawns an enabled Camera of its own turns the free-fly camera off until F3.
         */
        std::function<void(Fenrir::App& app, Fenrir::Scene& scene)> spawn;
        /** @brief Runs once a frame while the scene is active, in Update, on the main thread. Optional. */
        std::function<void(Fenrir::App& app, Fenrir::Scene& scene)> update;
        /**
         * @brief Nonzero marks a C# scene, spawned and run by the app's C# systems, which know it by this number: each
         * time it is entered its FenrirScene gets a CSharpScene resource holding it, and the systems run only while
         * that is the active scene's (`CSharpScene.IsActive` in the Fenrir.Demo assembly). Unique in the app.
         */
        std::uint32_t csharpScene = 0;
    };

    /**
     * @brief The marker a C# scene's FenrirScene holds (DemoScene::csharpScene), a scene resource C# reads as
     * `Fenrir.Demo.CSharpScene`: which C# scene it is, and which entry, so that its systems run only while it is active
     * and spawn it once each time it is entered.
     */
    struct CSharpScene
    {
        /** @brief The scene's DemoScene::csharpScene. */
        std::uint32_t scene = 0;
        /** @brief Counts the app's scene entries: a new value each time any scene is entered. */
        std::uint32_t entry = 0;
    };

    /** @brief The smoke-run switches, read from the environment by FromEnvironment (examples/DemoScaffold/README.md).
     */
    struct DemoRunOptions
    {
        /** @brief FENRIR_DEMO_SCENE: the scene to start in, or the one scene a smoke run runs. */
        std::optional<std::string> scene;
        /**
         * @brief FENRIR_DEMO_KEYS=1: press the starting scene's keys, two seconds in and then one a second, and stop
         * three seconds after the last.
         */
        bool pressKeys = false;
        /**
         * @brief FENRIR_DEMO_SMOKE=<frames>: run every scene (or `scene`) this many frames with its keys pressed, then
         * stop; Run returns non-zero if anything logged an error.
         */
        std::optional<std::uint32_t> smokeFrames;
        /** @brief FENRIR_DEMO_BACKEND=null|opengl. */
        Fenrir::GraphicsBackend backend = Fenrir::GraphicsBackend::OpenGL;

        /**
         * @brief The switches as this process's environment sets them. A value that cannot be read is ignored, and
         * the reason is added to `problems`, for the scaffold to log once the app's logger is reachable.
         */
        [[nodiscard]] static DemoRunOptions FromEnvironment(std::vector<std::string>* problems = nullptr);
    };

    /** @brief How DemoScaffold::Install sets a demo app up. */
    struct DemoConfig
    {
        /** @brief The app's name: the window's title, the demo panel's title and the help's first line. */
        std::string appName = "Demo";
        /**
         * @brief The window's icon, an RGBA8 image (PNG or JPEG) loaded through the asset store, relative to its root:
         * by default Fenrir's icon from the shared assets (examples/assets/icons). Empty keeps SDL's default icon.
         */
        std::string iconPath = "icons/fenrir.png";
        /** @brief The app's key rows (F2 and anything app-wide), in the help after the scaffold's. */
        std::vector<DemoKeyRow> appKeyRows;
        /** @brief The ordered scene list; PageDown and PageUp step through it, wrapping. */
        std::vector<DemoScene> scenes;
        /**
         * @brief Adds the demo panel's "Frame" section, after the scene switcher: the frame time and fps, averaged
         * over half a second, and the draws, instances and shadow draws of the renderer's last frame (Renderer::Stats).
         */
        bool frameStats = false;
        /** @brief The renderer's MSAA samples per pixel (RendererConfig::msaa); 1 is off. */
        std::uint32_t msaa = 1;
        /**
         * @brief The demo UI kit's 2D target: the tests' seam, which captures it. Null, the app's, draws it on the main
         * window's overlay.
         */
        std::shared_ptr<Fenrir::UITarget> demoUITarget;
        /** @brief The smoke-run switches; DemoScaffold reads them from the environment unless given. */
        std::optional<DemoRunOptions> run;
        /**
         * @brief The app's C# script assembly, relative to the executable's directory (fenrir_add_demo_app's SCRIPTS
         * builds it to `scripts/<app>.Scripts.dll`, and defines FENRIR_DEMO_SCRIPT_ASSEMBLY to that path for the
         * app's sources). Empty, or with scripting not built, the app has no C# systems.
         */
        std::filesystem::path scriptAssembly;
        /**
         * @brief Installs the app's own packages that need the scaffold's (FenrirSkeleton and FenrirAnimation need
         * FenrirAssets; a UI root needs FenrirUI) and must come before Scripting (a script's pose stage systems are
         * placed when it loads): run by Install after the renderer, FenrirUI and the demo UI kit, and before Scripting.
         * What it returns holds the packages' owners: the scaffold keeps it, and destroys it after Scripting ends and
         * before its own packages. Empty, nothing runs.
         */
        std::function<std::shared_ptr<void>(Fenrir::App& app)> installPackages;
#ifdef FENRIR_DEMO_SCRIPTING
        /**
         * @brief The app's own scripting extensions (its components and events shared with C#), which Scripting runs
         * with after the packages' (the text, painter and UI ones among them) and the scaffold's. Only with scripting
         * built (FENRIR_DEMO_SCRIPTING).
         */
        std::vector<Fenrir::ScriptingExtension> scriptingExtensions;
#endif
    };

    /**
     * @brief Sent when the scene is left-clicked (without the right mouse button held): the ray, from
     * Renderer::ScreenPointToRay, from the camera drawn at the clicked point (Renderer::CameraAt) through it. A click
     * where no camera draws sends nothing.
     */
    struct DemoSceneClickEvent
    {
        Fenrir::Math::Ray ray;
        /** @brief Where the click was, in the window's logical coordinates. */
        Fenrir::Math::Vec2 position{0.0f, 0.0f};
    };

    /**
     * @brief How the free-fly camera looks, an app resource DemoScaffold::Install adds: a game's settings change it
     * while the app runs.
     */
    struct FreeFlyLook
    {
        /** @brief Moving the mouse up turns the camera down. */
        bool invertPitch = false;
    };

    /** @brief The exit code of a smoke run that could not start its backend, which CTest reports as skipped. */
    inline constexpr int SkippedExitCode = 4;

    /**
     * @brief The rotation of a yaw about +Y, then a pitch about the turned +X, in radians: the way DemoCameraStart's
     * yaw and pitch turn a camera. Positive yaw turns -Z towards -X (to the left, seen from above), and positive pitch
     * turns it towards +Y (up), so a camera with this rotation looks along -Z turned left by `yaw` and up by `pitch`.
     */
    [[nodiscard]] Fenrir::Math::Quat YawPitch(float yaw, float pitch);

    /**
     * @brief A quad of `width` by `height`, centred on the origin in the XY plane and facing +Z, for screens, boards
     * and other flat panels: four vertices with normal +Z, tangent +X (bitangent sign +1) and UV0 (0, 0) at the
     * top-left corner and (`repeat`, `repeat`) at the bottom-right, v growing down, so an image is drawn the right
     * way up and tiled `repeat` times across each side.
     */
    [[nodiscard]] Fenrir::MeshData ScreenQuad(float width, float height, float repeat = 1.0f);

    /** @brief What one save of a demo scene wrote, which a smoke run's round trip compares. */
    struct SceneRoundTripCounts
    {
        /** @brief The entities written, a block apiece. */
        std::size_t entities = 0;
        /**
         * @brief The components written that are live in the scene: those a load kept opaque, which a save writes
         * back, are not counted, since the scene lost them.
         */
        std::size_t components = 0;
    };

    /**
     * @brief The error a smoke run logs when the demo scene `scene` did not load back whole: `saved` counts the save of
     * the scene as it ran, and `reloaded` the save of the scene loaded back from that file. Empty when they match.
     */
    [[nodiscard]] std::optional<std::string> RoundTripMismatch(std::string_view scene,
                                                               const SceneRoundTripCounts& saved,
                                                               const SceneRoundTripCounts& reloaded);

    /**
     * @brief The owner of a demo app's engine packages and scaffold (GLOSSARY.md: demo scaffold). Move-only; destroy it
     * before the App.
     *
     * Keys: PageDown and PageUp step to the next and previous scene, wrapping; F1 hides or shows the demo UI kit; F3
     * turns the free-fly camera on or off (back to the scene's own camera). F2 is the app's. Every other key is the
     * scene's, but the free-fly camera moves with WASD only while the right mouse button is held.
     */
    class DemoScaffold
    {
      public:
        /**
         * @brief Installs Input, Assets, DebugDrawing, the Renderer on `config.run`'s backend and FenrirUI, and
         * FenrirWindow before them for any backend but Null, adds the demo UI kit (the DemoUI app resource), and adds
         * the scaffold's systems. When scripting is built (FENRIR_ENABLE_SCRIPTING) and `config.scriptAssembly` is
         * given, it installs Scripting last, running that assembly with every installed package's scripting extension
         * (the text, painter and UI ones too) and the scaffold's own (the Fenrir.Demo assembly: CSharpScene and
         * DemoSceneClickEvent), then `config.scriptingExtensions`; a script assembly that fails to load is logged as
         * an error. The first frame enters the scene FENRIR_DEMO_SCENE names, or the first.
         * A backend that cannot start leaves the renderer invalid (logged), and Run then returns without running.
         */
        [[nodiscard]] static DemoScaffold Install(Fenrir::App& app, DemoConfig config);

        DemoScaffold(DemoScaffold&& other) noexcept;
        DemoScaffold& operator=(DemoScaffold&& other) noexcept;
        DemoScaffold(const DemoScaffold&) = delete;
        DemoScaffold& operator=(const DemoScaffold&) = delete;
        /**
         * @brief Destroys the active demo scene, then Scripting, the app's packages, the demo UI kit and the engine
         * packages, FenrirUI and the renderer first.
         */
        ~DemoScaffold();

        /**
         * @brief Runs the app until it stops, and returns the process's exit code. A smoke run runs frames of 1/60 s,
         * stops after its last scene, and returns 1 if anything logged an error (GL debug errors included), else 0.
         * When the renderer could not start, returns SkippedExitCode for a smoke run and 1 otherwise, without running.
         */
        [[nodiscard]] int Run();

        /** @brief Whether the renderer started on the chosen backend. */
        [[nodiscard]] bool RendererStarted() const;

        /** @brief The free-fly camera's entity in the active demo scene; null before the first scene is entered. */
        [[nodiscard]] Fenrir::Entity FreeFlyCamera() const;

        /** @brief Whether the free-fly camera is the one drawing (F3 toggles it). */
        [[nodiscard]] bool FreeFlyCameraOn() const;

      private:
        explicit DemoScaffold(std::shared_ptr<Detail::DemoState> state);

        // The scaffold's systems hold it weakly: once the owner is gone they do nothing.
        std::shared_ptr<Detail::DemoState> m_state;
    };
} // namespace FenrirDemo
