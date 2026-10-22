#pragma once

#include "FenrirInput/InputTypes.hpp"
#include "FenrirWindow/WindowTypes.hpp"

namespace Fenrir
{
    class App;

    /**
     * @brief The tests' one way inside FenrirWindow: it stands in for the user and the OS by pushing SDL events onto
     * SDL's queue, which the next frame's pump takes like any other, and by resizing a window as the OS would. Neutral
     * types only, so the tests need no SDL. None of it is part of the interface game code sees.
     */
    struct WindowProbe
    {
        /** @brief Pushes the key at `scancode` going down in `window` (only Space and A are known to the probe). */
        static void PushKeyDown(App& app, WindowId window, Scancode scancode);
        /** @brief Pushes the close request of `window`, as its close button would. */
        static void PushCloseRequest(App& app, WindowId window);
        /** @brief Pushes `window` gaining the input focus. */
        static void PushFocusGained(App& app, WindowId window);
        /** @brief Resizes `window` to `logicalSize` and waits for the OS to apply it. False if SDL refuses. */
        static bool Resize(App& app, WindowId window, WindowSize logicalSize);
    };
} // namespace Fenrir
