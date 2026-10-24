#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "FenrirInput/Input.hpp"
#include "FenrirWindow/WindowTypes.hpp"

namespace Fenrir
{
    class App;

    namespace Detail
    {
        /** @brief One window FenrirWindow keeps: what it was asked for, its SDL window once made, and its surface. */
        struct WindowRecord
        {
            WindowId id;
            std::string title;
            WindowSize requestedSize;
            CursorMode cursorMode = CursorMode::Normal;
            CursorShape cursorShape = CursorShape::Arrow;
            /** @brief The icon last set: RGBA8 pixels of `iconSize`, rows top to bottom; empty for SDL's default. */
            WindowSize iconSize;
            std::vector<std::byte> iconPixels;

            SDL_Window* sdl = nullptr;
            /** @brief The graphics surface, while held: today always an OpenGL context. */
            SDL_GLContext context = nullptr;

            // As of the latest PreUpdate.
            WindowSize logicalSize;
            WindowSize pixelSize;
            float contentScale = 1.0f;
            bool minimised = false;
            bool focused = false;

            /** @brief The textInput claimant's caret last given to SDL as the text input area, while one is shown. */
            CaretRect caret;
            bool caretShown = false;
        };

        /**
         * @brief Everything Window::Install set up for one app: SDL, the windows, cursors and gamepads, and a share of
         * the app's input. Destroying it destroys the windows and quits SDL.
         */
        struct WindowState
        {
            WindowState(App& app, WindowConfig config, Input input);
            WindowState(const WindowState&) = delete;
            WindowState& operator=(const WindowState&) = delete;
            ~WindowState();

            App* app;
            WindowConfig config;
            std::vector<WindowRecord> windows;
            std::array<SDL_Cursor*, static_cast<std::size_t>(CursorShape::Hidden)> cursors{};
            std::vector<SDL_Gamepad*> gamepads;
            // Destroyed after SDL quits: the window's input lives at least as long as the window.
            Input input;

            /** @brief The record of `id`, or null. */
            [[nodiscard]] WindowRecord* Find(WindowId id);
            /** @brief The record of the SDL window `sdlId`, or null for a window FenrirWindow did not make. */
            [[nodiscard]] WindowRecord* FindSdl(SDL_WindowID sdlId);
        };

        /** @brief The app resource through which Window's statics, and a second Install, find the installed state. */
        struct InstalledWindow
        {
            std::weak_ptr<WindowState> state;
        };

        /** @brief The app's live WindowState, or null when it has no valid Window. */
        [[nodiscard]] WindowState* FindWindowState(App& app);
    } // namespace Detail
} // namespace Fenrir
