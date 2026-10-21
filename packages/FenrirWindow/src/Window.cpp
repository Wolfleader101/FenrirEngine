// SPIKE (prototype/fenrir-renderer-spike, throwaway): the only file that includes SDL.

#include "FenrirWindow/Window.hpp"

#include "FenrirApp/App.hpp"
#include "FenrirInput/Input.hpp"

#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>

#include <chrono>
#include <thread>
#include <utility>

namespace Fenrir
{
    namespace Detail
    {
        struct WindowState
        {
            WindowConfig config;
            SDL_Window* window = nullptr;
            SDL_GLContext context = nullptr;
            bool sdlStarted = false;
            bool minimised = false;
        };
    } // namespace Detail

    namespace
    {
        Detail::WindowState* State(App& app)
        {
            return app.GetResource<Detail::WindowState>();
        }

        bool CreateMainWindow(Detail::WindowState& state, SDL_WindowFlags apiFlag)
        {
            SDL_WindowFlags flags = apiFlag | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
            if (!state.config.visible)
            {
                flags |= SDL_WINDOW_HIDDEN;
            }
            state.window = SDL_CreateWindow(state.config.title.c_str(), state.config.width, state.config.height, flags);
            return state.window != nullptr;
        }

        Key ToKey(SDL_Scancode code)
        {
            switch (code)
            {
            case SDL_SCANCODE_W:
                return Key::W;
            case SDL_SCANCODE_A:
                return Key::A;
            case SDL_SCANCODE_S:
                return Key::S;
            case SDL_SCANCODE_D:
                return Key::D;
            case SDL_SCANCODE_Q:
                return Key::Q;
            case SDL_SCANCODE_E:
                return Key::E;
            case SDL_SCANCODE_V:
                return Key::V;
            case SDL_SCANCODE_LSHIFT:
                return Key::LeftShift;
            case SDL_SCANCODE_SPACE:
                return Key::Space;
            case SDL_SCANCODE_ESCAPE:
                return Key::Escape;
            case SDL_SCANCODE_F1:
                return Key::F1;
            case SDL_SCANCODE_F2:
                return Key::F2;
            default:
                return Key::Unknown;
            }
        }

        void PumpEvents(App& app)
        {
            Detail::WindowState* state = State(app);
            if (state == nullptr)
            {
                return;
            }
            InputFeed::BeginFrame(app);
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                switch (event.type)
                {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    if (state->config.stopOnMainWindowClose)
                    {
                        app.Stop();
                    }
                    break;
                case SDL_EVENT_WINDOW_MINIMIZED:
                    state->minimised = true;
                    break;
                case SDL_EVENT_WINDOW_RESTORED:
                case SDL_EVENT_WINDOW_MAXIMIZED:
                    state->minimised = false;
                    break;
                case SDL_EVENT_KEY_DOWN:
                case SDL_EVENT_KEY_UP:
                    if (!event.key.repeat)
                    {
                        InputFeed::SetKey(app, ToKey(event.key.scancode), event.type == SDL_EVENT_KEY_DOWN);
                    }
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    InputFeed::MouseMoved(app, event.motion.x, event.motion.y, event.motion.xrel, event.motion.yrel);
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_UP: {
                    const int button = event.button.button == SDL_BUTTON_LEFT    ? 0
                                       : event.button.button == SDL_BUTTON_RIGHT ? 1
                                                                                 : 2;
                    InputFeed::SetMouseButton(app, button, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                    break;
                }
                default:
                    break;
                }
            }
            if (state->minimised && state->config.minimisedFrameSleep > 0.0)
            {
                std::this_thread::sleep_for(std::chrono::duration<double>(state->config.minimisedFrameSleep));
            }
        }
    } // namespace

    Window Window::Install(App& app, WindowConfig config)
    {
        Detail::WindowState& state = app.AddResource<Detail::WindowState>();
        state.config = std::move(config);
        SDL_SetMainReady(); // no SDL_main (ADR-0020)
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        {
            app.Logger()->Error(std::string("FenrirWindow: SDL_Init failed: ") + SDL_GetError());
        }
        else
        {
            state.sdlStarted = true;
        }

        // Both on the main thread: SDL's event pump and window calls must be (ADR-0020). AddSystem would run them on a
        // worker.
        app.AddSequentialSystems(SchedulePriority::PreInit, {[](App& a) {
                                     Detail::WindowState* s = State(a);
                                     if (s != nullptr && s->sdlStarted && s->window == nullptr)
                                     {
                                         CreateMainWindow(*s, 0); // nothing asked for a surface: a plain window
                                     }
                                 }});
        app.AddSequentialSystems(SchedulePriority::PreUpdate, {PumpEvents});
        return Window(app);
    }

    Window::Window(App& app) : m_app(&app)
    {
    }

    Window::Window(Window&& other) noexcept : m_app(std::exchange(other.m_app, nullptr))
    {
    }

    Window& Window::operator=(Window&& other) noexcept
    {
        std::swap(m_app, other.m_app);
        return *this;
    }

    Window::~Window()
    {
        if (m_app == nullptr)
        {
            return;
        }
        if (Detail::WindowState* state = State(*m_app))
        {
            if (state->context != nullptr)
            {
                m_app->Logger()->Error("FenrirWindow: destroyed while its surface is still held");
                SDL_GL_DestroyContext(state->context);
            }
            if (state->window != nullptr)
            {
                SDL_DestroyWindow(state->window);
            }
            if (state->sdlStarted)
            {
                SDL_Quit();
            }
            m_app->RemoveResource<Detail::WindowState>();
        }
    }

    bool Window::RequestSurface(App& app, WindowId window, const SurfaceRequest& request, std::string& error)
    {
        Detail::WindowState* state = State(app);
        if (state == nullptr || !state->sdlStarted || window != MainWindow())
        {
            error = "FenrirWindow is not installed, or the window is not the main window";
            return false;
        }
        if (state->window != nullptr)
        {
            error = "the main window already exists";
            return false;
        }
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, request.glMajor);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, request.glMinor);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, request.debugContext ? SDL_GL_CONTEXT_DEBUG_FLAG : 0);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, request.srgbBackbuffer ? 1 : 0);
        if (!CreateMainWindow(*state, SDL_WINDOW_OPENGL))
        {
            error = SDL_GetError();
            return false;
        }
        state->context = SDL_GL_CreateContext(state->window);
        if (state->context == nullptr)
        {
            error = SDL_GetError();
            SDL_DestroyWindow(state->window);
            state->window = nullptr;
            return false;
        }
        return true;
    }

    void Window::ReleaseSurface(App& app, WindowId)
    {
        Detail::WindowState* state = State(app);
        if (state != nullptr && state->context != nullptr)
        {
            SDL_GL_DestroyContext(state->context);
            state->context = nullptr;
        }
    }

    void* Window::GetProcAddress(App&, const char* name)
    {
        return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
    }

    bool Window::MakeCurrent(App& app, WindowId)
    {
        Detail::WindowState* state = State(app);
        return state != nullptr && SDL_GL_MakeCurrent(state->window, state->context);
    }

    void Window::Present(App& app, WindowId)
    {
        Detail::WindowState* state = State(app);
        if (state != nullptr && state->window != nullptr && state->config.visible)
        {
            SDL_GL_SwapWindow(state->window);
        }
    }

    bool Window::SetSwapInterval(App&, int interval)
    {
        return SDL_GL_SetSwapInterval(interval);
    }

    PixelSize Window::GetPixelSize(App& app, WindowId)
    {
        PixelSize size;
        Detail::WindowState* state = State(app);
        if (state != nullptr && state->window != nullptr)
        {
            SDL_GetWindowSizeInPixels(state->window, &size.width, &size.height);
        }
        return size;
    }

    bool Window::IsMinimised(App& app, WindowId)
    {
        Detail::WindowState* state = State(app);
        return state == nullptr || state->minimised;
    }

    bool Window::IsInstalled(App& app)
    {
        return State(app) != nullptr;
    }

    void Window::SetRelativeMouseMode(App& app, WindowId, bool on)
    {
        Detail::WindowState* state = State(app);
        if (state != nullptr && state->window != nullptr)
        {
            SDL_SetWindowRelativeMouseMode(state->window, on);
        }
    }

    void Window::SetTitle(App& app, WindowId, const std::string& title)
    {
        Detail::WindowState* state = State(app);
        if (state != nullptr && state->window != nullptr)
        {
            SDL_SetWindowTitle(state->window, title.c_str());
        }
    }
} // namespace Fenrir
