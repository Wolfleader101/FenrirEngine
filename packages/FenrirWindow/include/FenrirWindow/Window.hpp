#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "FenrirInput/InputTypes.hpp"
#include "FenrirWindow/WindowEvents.hpp"
#include "FenrirWindow/WindowTypes.hpp"

namespace Fenrir
{
    class App;
    // Only named here: FenrirWindowScripting defines Window::ScriptingExtension().
    struct ScriptingExtension;

    namespace Detail
    {
        /** @brief What Window::Install set up. Defined in FenrirWindow's sources; nothing else can use it. */
        struct WindowState;
    } // namespace Detail

    /**
     * @brief The owner of FenrirWindow in an app (ADR-0020, ADR-0022): SDL, the main window and its graphics surface,
     * and the pump that feeds FenrirInput. Move-only.
     *
     * Install it before the Renderer and destroy it after (declaring the owners in install order does this), and before
     * the App. Destroying it destroys the windows and quits SDL. Everything FenrirWindow does runs on the thread that
     * runs the App, the main thread, as SDL requires: call its functions from there, outside parallel systems.
     *
     * Its functions are statics taking the App, so the owner can move: they reach the state through the App, and do
     * nothing (or return empty values) when the app has no valid Window. Windows are keyed by WindowId; the foundation
     * makes the main window only.
     */
    class Window
    {
      public:
        /**
         * @brief Starts SDL's video (and gamepads) for `app` and keeps `config`, but makes no window. Installs
         * FenrirInput if the app has none yet, and adds the pump: a sequential PreUpdate system that takes SDL's
         * events, feeds them to the input feed, calls Input::Dispatch, puts each window's text input as the input
         * routers' textInput claims say, and updates each window's sizes. If no renderer requested the main window's
         * surface by then, a plain window (no graphics API) is made in the first frame's PreInit, so install it before
         * the first frame.
         *
         * When SDL's video cannot start (no display), or the app already has a Window, it logs why and returns an owner
         * whose IsValid() is false: it added no systems, and FenrirWindow's functions do nothing for the app.
         */
        [[nodiscard]] static Window Install(App& app, WindowConfig config = {});

        Window(Window&& other) noexcept;
        Window& operator=(Window&& other) noexcept;
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        /** @brief Destroys the windows and quits SDL. A window's graphics surface still held then is a logged error. */
        ~Window();

        /** @brief True for an owner Install made with SDL running; false when Install failed, or once moved from. */
        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_state != nullptr;
        }

        /** @brief The main window's id, whether or not the window exists yet. */
        [[nodiscard]] static constexpr WindowId MainWindow() noexcept
        {
            return WindowId{1};
        }

        /** @brief Whether `window` has been made (the main window: by a surface request or in the first PreInit). */
        [[nodiscard]] static bool Exists(App& app, WindowId window);

        /** @brief `window`'s size in logical units, as of this frame's PreUpdate; zero before it exists. */
        [[nodiscard]] static WindowSize LogicalSize(App& app, WindowId window);
        /** @brief `window`'s size in pixels, as of this frame's PreUpdate; zero before it exists. */
        [[nodiscard]] static WindowSize PixelSize(App& app, WindowId window);
        /** @brief `window`'s pixels per logical unit, as of this frame's PreUpdate; 1 before it exists. */
        [[nodiscard]] static float ContentScale(App& app, WindowId window);
        /** @brief Whether `window` is minimised, as of this frame's PreUpdate: nothing should be drawn to it. */
        [[nodiscard]] static bool IsMinimised(App& app, WindowId window);
        /** @brief Whether `window` has the input focus, as of this frame's PreUpdate; false before it exists. */
        [[nodiscard]] static bool IsFocused(App& app, WindowId window);

        /** @brief Sets `window`'s title (or the title it will be made with). False if SDL refuses. */
        static bool SetTitle(App& app, WindowId window, std::string_view title);
        /**
         * @brief Sets `window`'s icon, shown in its title bar and the taskbar (or the icon it will be made with), from
         * `rgba8`: `width * height` pixels of four bytes (red, green, blue, alpha), rows top to bottom. False, with
         * one logged error, for a window FenrirWindow does not keep, an empty size, pixels of the wrong size, or if
         * SDL refuses.
         */
        static bool SetIcon(App& app, WindowId window, int width, int height, std::span<const std::byte> rgba8);
        /** @brief Sets how the cursor behaves over `window`. False if SDL refuses. */
        static bool SetCursorMode(App& app, WindowId window, CursorMode mode);
        /** @brief How the cursor behaves over `window`, as last set; Normal for a window FenrirWindow does not keep. */
        [[nodiscard]] static CursorMode GetCursorMode(App& app, WindowId window);
        /** @brief Sets the cursor shown over `window`. False if SDL refuses. */
        static bool SetCursorShape(App& app, WindowId window, CursorShape shape);

        /** @brief The system clipboard's text, or empty if it holds none. */
        [[nodiscard]] static std::string ClipboardText(App& app);
        /** @brief Puts `text` on the system clipboard. False if SDL refuses. */
        static bool SetClipboardText(App& app, std::string_view text);

        /**
         * @brief Asks for `window` to close, as its close button does: sends WindowCloseRequestedEvent, and for the
         * main window with WindowConfig::stopOnMainWindowClose stops the app. The window itself stays until teardown.
         * False, doing nothing, if `window` has not been made.
         */
        static bool RequestClose(App& app, WindowId window);

        /**
         * @brief What the window gives C# (ADR-0029): the `Fenrir.Window` contract, with the native functions behind
         * its `Window` class (sizes, focus, cursor, clipboard, title, close) and the resized and close-requested
         * events. Add it to ScriptingConfig::extensions after Input::ScriptingExtension(), whose contract it uses (its
         * WindowId and focus event).
         *
         * It needs no owner: its functions read the app the running scripts serve, so scripts always load. With no
         * Window, or for a window that has not been made, they do nothing and return false, zero or empty. In debug
         * builds each such case is warned about once per scripting install for each place in FenrirWindowScripting's
         * native code that notices it, not once per C# call site.
         *
         * Defined by the FenrirWindowScripting library, not FenrirWindow (which never links FenrirScripting), so it
         * exists only in a build with the renderer and scripting on. Include
         * "FenrirWindowScripting/WindowScripting.hpp" and link FenrirWindowScripting to call it.
         */
        [[nodiscard]] static Fenrir::ScriptingExtension ScriptingExtension();

        // The graphics surface (ADR-0022): the renderer's adapter makes, uses and releases it through these. Neutral
        // types only, so neither SDL nor a graphics API's headers reach the renderer.

        /**
         * @brief Makes `window` with a graphics surface for `request`: for OpenGL, a 4.6 core context with the
         * backbuffer described. Call it before the first frame (the renderer does, in its Install): a window made
         * without one cannot gain it. False, with SDL's message in `error`, if the window or its surface cannot be
         * made. The context is current on the main thread afterwards.
         */
        static bool RequestSurface(App& app, WindowId window, const SurfaceRequest& request, std::string& error);
        /** @brief Releases `window`'s graphics surface (the renderer does, when it is destroyed). The window stays. */
        static void ReleaseSurface(App& app, WindowId window);
        /** @brief Whether `window` holds a graphics surface. */
        [[nodiscard]] static bool HasSurface(App& app, WindowId window);
        /** @brief The address of the graphics API function `name` for `window`'s surface, or null. */
        [[nodiscard]] static void* GetProcAddress(App& app, WindowId window, const char* name);
        /** @brief Makes `window`'s surface current on this thread. False without a surface or if SDL refuses. */
        static bool MakeCurrent(App& app, WindowId window);
        /** @brief Shows what was drawn to `window`'s backbuffer. Does nothing for a hidden window or one with none. */
        static void Present(App& app, WindowId window);
        /**
         * @brief Sets how presents of `window`'s surface wait for the display: 0 not at all, 1 for vertical sync, -1
         * adaptive (late swaps tear). False without a surface or if the driver refuses that interval.
         */
        static bool SetSwapInterval(App& app, WindowId window, int interval);

      private:
        explicit Window(std::shared_ptr<Detail::WindowState> state);

        // The pump and PreInit hold it weakly: once the owner is gone they do nothing.
        std::shared_ptr<Detail::WindowState> m_state;
    };
} // namespace Fenrir
