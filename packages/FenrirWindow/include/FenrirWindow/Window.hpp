#pragma once

// SPIKE (prototype/fenrir-renderer-spike, throwaway): FenrirWindow as ADR-0022 describes it, main window only. Public
// headers name neither SDL nor GL: proc addresses are void*, sizes are ints.

#include <cstdint>
#include <memory>
#include <string>

namespace Fenrir
{
    class App;

    using WindowId = std::uint32_t;

    struct WindowConfig
    {
        std::string title = "Fenrir";
        int width = 1280;
        int height = 720;
        bool visible = true;
        bool stopOnMainWindowClose = true;
        /** @brief Seconds slept at the end of PreUpdate while no window can be drawn. */
        double minimisedFrameSleep = 0.007;
    };

    /** @brief What the renderer asks FenrirWindow to make (ADR-0022). Only OpenGL in the spike. */
    struct SurfaceRequest
    {
        enum class Api
        {
            OpenGL,
        };
        Api api = Api::OpenGL;
        int glMajor = 4;
        int glMinor = 6;
        bool debugContext = false;
        bool srgbBackbuffer = true;
    };

    struct PixelSize
    {
        int width = 0;
        int height = 0;
    };

    namespace Detail
    {
        struct WindowState;
    }

    /**
     * @brief The owner of FenrirWindow's state (SDL, the windows and their surfaces, held in an app resource).
     * Move-only. Destroying it destroys the windows and quits SDL. Destroy it after the Renderer and before the App.
     */
    class Window
    {
      public:
        /** @brief Starts SDL's video and events. Creates no window yet: the first surface request does, or PreInit. */
        [[nodiscard]] static Window Install(App& app, WindowConfig config = {});

        Window(Window&&) noexcept;
        Window& operator=(Window&&) noexcept;
        ~Window();

        [[nodiscard]] static constexpr WindowId MainWindow()
        {
            return 1;
        }

        // The surface functions the renderer's adapter uses (ADR-0022). Static and reached through the App, since the
        // owner is a value that moves: the state lives in an app resource Install adds.
        /** @brief Creates the main window with a surface for `request`. False, with SDL's message, if it cannot. */
        static bool RequestSurface(App& app, WindowId window, const SurfaceRequest& request, std::string& error);
        static void ReleaseSurface(App& app, WindowId window);
        [[nodiscard]] static void* GetProcAddress(App& app, const char* name);
        static bool MakeCurrent(App& app, WindowId window);
        static void Present(App& app, WindowId window);
        /** @brief 0 off, 1 on, -1 adaptive. False if the driver refuses. */
        static bool SetSwapInterval(App& app, int interval);
        [[nodiscard]] static PixelSize GetPixelSize(App& app, WindowId window);
        [[nodiscard]] static bool IsMinimised(App& app, WindowId window);
        [[nodiscard]] static bool IsInstalled(App& app);

        static void SetRelativeMouseMode(App& app, WindowId window, bool on);
        static void SetTitle(App& app, WindowId window, const std::string& title);

      private:
        explicit Window(App& app);
        App* m_app = nullptr;
    };
} // namespace Fenrir
