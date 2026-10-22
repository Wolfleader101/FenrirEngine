#include "WindowProbe.hpp"

#include <SDL3/SDL.h>

#include "WindowState.hpp"

namespace Fenrir
{
    namespace
    {
        // The SDL id of a made window, or 0.
        SDL_WindowID SdlId(App& app, WindowId window)
        {
            Detail::WindowState* state = Detail::FindWindowState(app);
            const Detail::WindowRecord* record = state == nullptr ? nullptr : state->Find(window);
            return record == nullptr || record->sdl == nullptr ? 0 : SDL_GetWindowID(record->sdl);
        }

        SDL_Window* SdlWindow(App& app, WindowId window)
        {
            Detail::WindowState* state = Detail::FindWindowState(app);
            const Detail::WindowRecord* record = state == nullptr ? nullptr : state->Find(window);
            return record == nullptr ? nullptr : record->sdl;
        }

        void PushWindowEvent(App& app, WindowId window, SDL_EventType type)
        {
            SDL_Event event{};
            event.window.type = type;
            event.window.timestamp = SDL_GetTicksNS();
            event.window.windowID = SdlId(app, window);
            SDL_PushEvent(&event);
        }
    } // namespace

    void WindowProbe::PushKeyDown(App& app, WindowId window, Scancode scancode)
    {
        SDL_Event event{};
        event.key.type = SDL_EVENT_KEY_DOWN;
        event.key.timestamp = SDL_GetTicksNS();
        event.key.windowID = SdlId(app, window);
        event.key.scancode = scancode == Scancode::A ? SDL_SCANCODE_A : SDL_SCANCODE_SPACE;
        event.key.key = SDL_GetKeyFromScancode(event.key.scancode, SDL_KMOD_NONE, false);
        event.key.down = true;
        SDL_PushEvent(&event);
    }

    void WindowProbe::PushCloseRequest(App& app, WindowId window)
    {
        PushWindowEvent(app, window, SDL_EVENT_WINDOW_CLOSE_REQUESTED);
    }

    void WindowProbe::PushFocusGained(App& app, WindowId window)
    {
        PushWindowEvent(app, window, SDL_EVENT_WINDOW_FOCUS_GAINED);
    }

    bool WindowProbe::Resize(App& app, WindowId window, WindowSize logicalSize)
    {
        Detail::WindowState* state = Detail::FindWindowState(app);
        const Detail::WindowRecord* record = state == nullptr ? nullptr : state->Find(window);
        if (record == nullptr || record->sdl == nullptr)
        {
            return false;
        }
        return SDL_SetWindowSize(record->sdl, logicalSize.width, logicalSize.height) && SDL_SyncWindow(record->sdl);
    }

    bool WindowProbe::IsTextInputOn(App& app, WindowId window)
    {
        SDL_Window* sdl = SdlWindow(app, window);
        return sdl != nullptr && SDL_TextInputActive(sdl);
    }

    CaretRect WindowProbe::TextInputArea(App& app, WindowId window)
    {
        SDL_Window* sdl = SdlWindow(app, window);
        SDL_Rect area{};
        int cursor = 0;
        if (sdl == nullptr || !SDL_GetTextInputArea(sdl, &area, &cursor))
        {
            return {};
        }
        return CaretRect{{static_cast<float>(area.x), static_cast<float>(area.y)},
                         {static_cast<float>(area.w), static_cast<float>(area.h)}};
    }
} // namespace Fenrir
