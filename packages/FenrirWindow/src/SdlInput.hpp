#pragma once

#include <SDL3/SDL.h>

#include "FenrirInput/InputFeed.hpp"
#include "FenrirInput/InputTypes.hpp"

namespace Fenrir::Detail
{
    struct WindowState;

    /**
     * @brief Translates one SDL input event into FenrirInput's types and feeds it (ADR-0017): keys, text, mouse
     * buttons, motion and scroll from FenrirWindow's windows, and gamepads connecting, disconnecting, and their
     * buttons and axes. Opens and closes the SDL gamepads as they come and go. False if `event` is not input.
     */
    bool FeedInput(WindowState& state, InputFeed& feed, const SDL_Event& event);

    /** @brief Closes every gamepad FeedInput opened. */
    void CloseGamepads(WindowState& state);
} // namespace Fenrir::Detail
