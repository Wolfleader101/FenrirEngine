#include "FenrirWindow/Window.hpp"

#include <SDL3/SDL.h>
// The engine keeps its own main (ADR-0020): SDL_main is not used.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <thread>
#include <utility>

#include "FenrirApp/App.hpp"
#include "SdlInput.hpp"
#include "WindowState.hpp"

namespace Fenrir
{
    namespace Detail
    {
        WindowState::WindowState(App& owningApp, WindowConfig windowConfig, Input sharedInput)
            : app(&owningApp), config(std::move(windowConfig)), input(std::move(sharedInput))
        {
            WindowRecord main;
            main.id = Window::MainWindow();
            main.title = config.title;
            main.requestedSize = config.logicalSize;
            windows.push_back(std::move(main));
        }

        WindowState::~WindowState()
        {
            for (WindowRecord& window : windows)
            {
                if (window.context != nullptr)
                {
                    app->Logger()->Error("FenrirWindow: window {} was destroyed while its graphics surface was still "
                                         "held; destroy the Renderer before the Window",
                                         window.id.value);
                    SDL_GL_DestroyContext(window.context);
                }
                if (window.sdl != nullptr)
                {
                    SDL_DestroyWindow(window.sdl);
                }
            }
            CloseGamepads(*this);
            for (SDL_Cursor* cursor : cursors)
            {
                if (cursor != nullptr)
                {
                    SDL_DestroyCursor(cursor);
                }
            }
            SDL_Quit();

            // Only this state's resource: a later Install's must stay.
            const InstalledWindow* installed = app->TryGetResource<InstalledWindow>();
            if (installed != nullptr && installed->state.expired())
            {
                app->RemoveResource<InstalledWindow>();
            }
        }

        WindowRecord* WindowState::Find(WindowId id)
        {
            const auto found =
                std::find_if(windows.begin(), windows.end(), [id](const WindowRecord& w) { return w.id == id; });
            return found == windows.end() ? nullptr : &*found;
        }

        WindowRecord* WindowState::FindSdl(SDL_WindowID sdlId)
        {
            if (sdlId == 0)
            {
                return nullptr;
            }
            const auto found = std::find_if(windows.begin(), windows.end(), [sdlId](const WindowRecord& w) {
                return w.sdl != nullptr && SDL_GetWindowID(w.sdl) == sdlId;
            });
            return found == windows.end() ? nullptr : &*found;
        }

        WindowState* FindWindowState(App& app)
        {
            const InstalledWindow* installed = app.TryGetResource<InstalledWindow>();
            if (installed == nullptr)
            {
                return nullptr;
            }
            // The owner keeps the state alive; a resource left by an ended owner names none.
            return installed->state.expired() ? nullptr : installed->state.lock().get();
        }
    } // namespace Detail

    namespace
    {
        using Detail::WindowRecord;
        using Detail::WindowState;

        WindowRecord* FindWindow(App& app, WindowId id)
        {
            WindowState* state = Detail::FindWindowState(app);
            return state == nullptr ? nullptr : state->Find(id);
        }

        // A made window, or null.
        WindowRecord* FindMade(App& app, WindowId id)
        {
            WindowRecord* window = FindWindow(app, id);
            return window != nullptr && window->sdl != nullptr ? window : nullptr;
        }

        // SDL's system cursor for each shown CursorShape, in the enum's order (Hidden shows none).
        constexpr std::array<SDL_SystemCursor, static_cast<std::size_t>(CursorShape::Hidden)> systemCursors{
            SDL_SYSTEM_CURSOR_DEFAULT,   SDL_SYSTEM_CURSOR_TEXT,        SDL_SYSTEM_CURSOR_EW_RESIZE,
            SDL_SYSTEM_CURSOR_NS_RESIZE, SDL_SYSTEM_CURSOR_NWSE_RESIZE, SDL_SYSTEM_CURSOR_NESW_RESIZE,
            SDL_SYSTEM_CURSOR_POINTER,   SDL_SYSTEM_CURSOR_NOT_ALLOWED,
        };

        // Puts a made window's cursor mode and shape into effect. One window is made, so SDL's app-wide cursor
        // (shown or hidden, and which one) follows it.
        bool ApplyCursor(WindowState& state, WindowRecord& window)
        {
            bool applied = SDL_SetWindowRelativeMouseMode(window.sdl, window.cursorMode == CursorMode::Captured);
            if (window.cursorShape != CursorShape::Hidden)
            {
                const auto index = static_cast<std::size_t>(window.cursorShape);
                SDL_Cursor*& cursor = state.cursors.at(index);
                if (cursor == nullptr)
                {
                    cursor = SDL_CreateSystemCursor(systemCursors.at(index));
                }
                applied = applied && cursor != nullptr && SDL_SetCursor(cursor);
            }
            const bool shown = window.cursorMode == CursorMode::Normal && window.cursorShape != CursorShape::Hidden;
            return (shown ? SDL_ShowCursor() : SDL_HideCursor()) && applied;
        }

        // Reads a made window's sizes and state from SDL. True if its logical size, pixel size or content scale
        // changed.
        bool ReadWindow(WindowRecord& window)
        {
            WindowSize logical;
            WindowSize pixels;
            SDL_GetWindowSize(window.sdl, &logical.width, &logical.height);
            SDL_GetWindowSizeInPixels(window.sdl, &pixels.width, &pixels.height);
            float scale = SDL_GetWindowDisplayScale(window.sdl);
            scale = scale > 0.0f ? scale : 1.0f;
            const SDL_WindowFlags flags = SDL_GetWindowFlags(window.sdl);
            window.minimised = (flags & SDL_WINDOW_MINIMIZED) != 0;
            window.focused = (flags & SDL_WINDOW_INPUT_FOCUS) != 0;

            // SDL gives the scale as a ratio of whole numbers, so it moves by far more than this when it changes.
            constexpr float scaleTolerance = 1e-4f;
            const bool changed = logical != window.logicalSize || pixels != window.pixelSize ||
                                 std::abs(scale - window.contentScale) > scaleTolerance;
            window.logicalSize = logical;
            window.pixelSize = pixels;
            window.contentScale = scale;
            return changed;
        }

        // Puts SDL's text input for a made window as ADR-0027 says: on, with the claimant's caret as the IME's text
        // input area, while a router claims textInput there; otherwise as textInputByDefault says.
        void ApplyTextInput(WindowState& state, WindowRecord& window)
        {
            const InputClaims* published = state.app->TryGetResource<InputClaims>();
            const WindowInputClaims claims = published == nullptr ? WindowInputClaims{} : published->For(window.id);
            if (claims.textInput && (!window.caretShown || window.caret != claims.caret))
            {
                // Whole pixels of logical units, rounded outwards so the area covers the caret.
                const int left = static_cast<int>(std::floor(claims.caret.position.x));
                const int top = static_cast<int>(std::floor(claims.caret.position.y));
                const int right = static_cast<int>(std::ceil(claims.caret.position.x + claims.caret.size.x));
                const int bottom = static_cast<int>(std::ceil(claims.caret.position.y + claims.caret.size.y));
                const SDL_Rect area{left, top, right - left, bottom - top};
                SDL_SetTextInputArea(window.sdl, &area, 0);
                window.caret = claims.caret;
                window.caretShown = true;
            }
            else if (!claims.textInput && window.caretShown)
            {
                SDL_SetTextInputArea(window.sdl, nullptr, 0);
                window.caretShown = false;
            }

            const bool on = claims.textInput || state.config.textInputByDefault;
            if (on && !SDL_TextInputActive(window.sdl))
            {
                SDL_StartTextInput(window.sdl);
            }
            else if (!on && SDL_TextInputActive(window.sdl))
            {
                SDL_StopTextInput(window.sdl);
            }
        }

        // Makes a window's SDL window, with `apiFlag` for its graphics surface (none for a plain window), and puts
        // what was asked of it before (title, cursor, text input) into effect.
        bool MakeWindow(WindowState& state, WindowRecord& window, SDL_WindowFlags apiFlag)
        {
            SDL_WindowFlags flags = apiFlag | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
            if (!state.config.visible)
            {
                flags |= SDL_WINDOW_HIDDEN;
            }
            window.sdl =
                SDL_CreateWindow(window.title.c_str(), window.requestedSize.width, window.requestedSize.height, flags);
            if (window.sdl == nullptr)
            {
                return false;
            }
            ReadWindow(window);
            if (window.cursorMode != CursorMode::Normal || window.cursorShape != CursorShape::Arrow)
            {
                ApplyCursor(state, window);
            }
            ApplyTextInput(state, window);
            return true;
        }

        void SleepSeconds(double seconds)
        {
            if (seconds > 0.0)
            {
                std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
            }
        }

        // FenrirWindow's PreUpdate: takes SDL's events, feeds the input to FenrirInput and dispatches it, puts each
        // window's text input as the routers' claims say, updates each window, and sleeps when nothing can be drawn.
        void Pump(App& app, WindowState& state)
        {
            InputFeed* feed = app.TryGetResource<InputFeed>();
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                if (feed != nullptr && Detail::FeedInput(state, *feed, event))
                {
                    continue;
                }
                switch (event.type)
                {
                case SDL_EVENT_WINDOW_FOCUS_GAINED:
                case SDL_EVENT_WINDOW_FOCUS_LOST:
                    if (const WindowRecord* window = state.FindSdl(event.window.windowID); window && feed)
                    {
                        if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
                        {
                            feed->FocusGained(window->id);
                        }
                        else
                        {
                            feed->FocusLost(window->id);
                        }
                    }
                    break;
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    if (const WindowRecord* window = state.FindSdl(event.window.windowID))
                    {
                        app.SendEvent(WindowCloseRequestedEvent{window->id});
                        if (window->id == Window::MainWindow() && state.config.stopOnMainWindowClose)
                        {
                            app.Stop();
                        }
                    }
                    break;
                case SDL_EVENT_QUIT:
                    // Only a signal (Ctrl+C) sends it, since closing the last window does not (Install's hint): it is
                    // the main window's close request.
                    app.SendEvent(WindowCloseRequestedEvent{Window::MainWindow()});
                    if (state.config.stopOnMainWindowClose)
                    {
                        app.Stop();
                    }
                    break;
                default:
                    break;
                }
            }

            Input::Dispatch(app);

            bool anyMade = false;
            bool anyDrawable = false;
            bool anyFocused = false;
            for (WindowRecord& window : state.windows)
            {
                if (window.sdl == nullptr)
                {
                    continue;
                }
                // After routing, so this frame's claims are in InputClaims.
                ApplyTextInput(state, window);
                if (ReadWindow(window))
                {
                    app.SendEvent(
                        WindowResizedEvent{window.id, window.logicalSize, window.pixelSize, window.contentScale});
                }
                anyMade = true;
                anyDrawable =
                    anyDrawable || (!window.minimised && window.pixelSize.width > 0 && window.pixelSize.height > 0);
                anyFocused = anyFocused || window.focused;
            }

            if (anyMade && !anyDrawable)
            {
                SleepSeconds(state.config.minimisedFrameSleep);
            }
            else if (anyMade && !anyFocused)
            {
                SleepSeconds(state.config.unfocusedFrameSleep);
            }
        }
    } // namespace

    Window Window::Install(App& app, WindowConfig config)
    {
        if (Detail::FindWindowState(app) != nullptr)
        {
            app.Logger()->Error("FenrirWindow: the app already has a Window; this second Install does nothing");
            return Window(nullptr);
        }

        SDL_SetMainReady();
        // A close request is FenrirWindow's to act on (stopOnMainWindowClose), not a quit of SDL's own.
        SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            app.Logger()->Error("FenrirWindow: SDL's video could not start, so no window can be made: {}",
                                SDL_GetError());
            return Window(nullptr);
        }
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
        {
            app.Logger()->Warn("FenrirWindow: SDL's gamepad support could not start, so no gamepad will connect: {}",
                               SDL_GetError());
        }

        auto state = std::make_shared<Detail::WindowState>(app, std::move(config), Input::Install(app));
        app.AddResource<Detail::InstalledWindow>(Detail::InstalledWindow{state});

        const std::weak_ptr<Detail::WindowState> weak = state;
        // Init phases run their systems on the app's thread, one after another.
        app.AddSystem(SchedulePriority::PreInit, [weak](App& running) {
            const std::shared_ptr<Detail::WindowState> live = weak.lock();
            if (live == nullptr)
            {
                return;
            }
            // Nothing requested a surface for the main window (no renderer, or the Null backend): a plain window.
            WindowRecord& main = *live->Find(MainWindow());
            if (main.sdl == nullptr && !MakeWindow(*live, main, 0))
            {
                running.Logger()->Error("FenrirWindow: the main window could not be made: {}", SDL_GetError());
            }
        });
        // Sequential, so on the main thread, as SDL's event pump must be (ADR-0020).
        app.AddSequentialSystem(SchedulePriority::PreUpdate, [weak](App& running) {
            if (const std::shared_ptr<Detail::WindowState> live = weak.lock())
            {
                Pump(running, *live);
            }
        });
        return Window(std::move(state));
    }

    Window::Window(std::shared_ptr<Detail::WindowState> state) : m_state(std::move(state))
    {
    }

    Window::Window(Window&& other) noexcept = default;
    Window& Window::operator=(Window&& other) noexcept = default;
    Window::~Window() = default;

    bool Window::Exists(App& app, WindowId window)
    {
        return FindMade(app, window) != nullptr;
    }

    WindowSize Window::LogicalSize(App& app, WindowId window)
    {
        const WindowRecord* made = FindMade(app, window);
        return made == nullptr ? WindowSize{} : made->logicalSize;
    }

    WindowSize Window::PixelSize(App& app, WindowId window)
    {
        const WindowRecord* made = FindMade(app, window);
        return made == nullptr ? WindowSize{} : made->pixelSize;
    }

    float Window::ContentScale(App& app, WindowId window)
    {
        const WindowRecord* made = FindMade(app, window);
        return made == nullptr ? 1.0f : made->contentScale;
    }

    bool Window::IsMinimised(App& app, WindowId window)
    {
        const WindowRecord* made = FindMade(app, window);
        return made != nullptr && made->minimised;
    }

    bool Window::SetTitle(App& app, WindowId window, std::string_view title)
    {
        WindowRecord* record = FindWindow(app, window);
        if (record == nullptr)
        {
            return false;
        }
        record->title = std::string(title);
        return record->sdl == nullptr || SDL_SetWindowTitle(record->sdl, record->title.c_str());
    }

    bool Window::SetCursorMode(App& app, WindowId window, CursorMode mode)
    {
        WindowRecord* record = FindWindow(app, window);
        if (record == nullptr)
        {
            return false;
        }
        record->cursorMode = mode;
        return record->sdl == nullptr || ApplyCursor(*Detail::FindWindowState(app), *record);
    }

    bool Window::SetCursorShape(App& app, WindowId window, CursorShape shape)
    {
        WindowRecord* record = FindWindow(app, window);
        if (record == nullptr)
        {
            return false;
        }
        record->cursorShape = shape;
        return record->sdl == nullptr || ApplyCursor(*Detail::FindWindowState(app), *record);
    }

    std::string Window::ClipboardText(App& app)
    {
        if (Detail::FindWindowState(app) == nullptr)
        {
            return {};
        }
        char* text = SDL_GetClipboardText();
        std::string copy = text == nullptr ? std::string() : std::string(text);
        SDL_free(text);
        return copy;
    }

    bool Window::SetClipboardText(App& app, std::string_view text)
    {
        return Detail::FindWindowState(app) != nullptr && SDL_SetClipboardText(std::string(text).c_str());
    }

    bool Window::RequestSurface(App& app, WindowId window, const SurfaceRequest& request, std::string& error)
    {
        WindowState* state = Detail::FindWindowState(app);
        WindowRecord* record = state == nullptr ? nullptr : state->Find(window);
        if (record == nullptr)
        {
            error = "FenrirWindow has no such window (is a valid Window installed?)";
            return false;
        }
        if (record->sdl != nullptr)
        {
            error = "the window was already made, without a graphics surface; request it before the first frame";
            return false;
        }

        // request.api is OpenGL, the only API built: a 4.6 core context and the backbuffer described.
        SDL_GL_ResetAttributes();
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, request.debugContext ? SDL_GL_CONTEXT_DEBUG_FLAG : 0);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, request.backbuffer.depthBits);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, request.backbuffer.stencilBits);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, request.backbuffer.samples > 0 ? 1 : 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, request.backbuffer.samples);
        SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, request.backbuffer.srgb ? 1 : 0);

        if (!MakeWindow(*state, *record, SDL_WINDOW_OPENGL))
        {
            error = SDL_GetError();
            return false;
        }
        record->context = SDL_GL_CreateContext(record->sdl);
        if (record->context == nullptr)
        {
            error = SDL_GetError();
            // Not made after all, so the first PreInit can still make it plain.
            SDL_DestroyWindow(record->sdl);
            record->sdl = nullptr;
            return false;
        }
        return true;
    }

    void Window::ReleaseSurface(App& app, WindowId window)
    {
        WindowRecord* record = FindWindow(app, window);
        if (record != nullptr && record->context != nullptr)
        {
            SDL_GL_DestroyContext(record->context);
            record->context = nullptr;
        }
    }

    bool Window::HasSurface(App& app, WindowId window)
    {
        const WindowRecord* record = FindWindow(app, window);
        return record != nullptr && record->context != nullptr;
    }

    void* Window::GetProcAddress(App& app, WindowId window, const char* name)
    {
        if (!HasSurface(app, window))
        {
            return nullptr;
        }
        return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
    }

    bool Window::MakeCurrent(App& app, WindowId window)
    {
        const WindowRecord* record = FindWindow(app, window);
        return record != nullptr && record->context != nullptr && SDL_GL_MakeCurrent(record->sdl, record->context);
    }

    void Window::Present(App& app, WindowId window)
    {
        WindowState* state = Detail::FindWindowState(app);
        const WindowRecord* record = state == nullptr ? nullptr : state->Find(window);
        if (record != nullptr && record->context != nullptr && state->config.visible)
        {
            SDL_GL_SwapWindow(record->sdl);
        }
    }

    bool Window::SetSwapInterval(App& app, WindowId window, int interval)
    {
        return MakeCurrent(app, window) && SDL_GL_SetSwapInterval(interval);
    }
} // namespace Fenrir
