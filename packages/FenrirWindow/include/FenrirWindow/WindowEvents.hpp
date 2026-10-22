#pragma once

#include "FenrirInput/InputTypes.hpp"
#include "FenrirWindow/WindowTypes.hpp"

// FenrirWindow's engine events, sent in PreUpdate and read through EventReader<T> (ADR-0012). Each names its window.
// A window gaining or losing the input focus is FenrirInput's WindowFocusEvent: FenrirWindow feeds focus to the input
// feed, so the event and Keyboard::FocusedWindow agree.

namespace Fenrir
{
    /** @brief A window's logical size, pixel size or content scale changed. Carries the new values. */
    struct WindowResizedEvent
    {
        WindowId window;
        WindowSize logicalSize;
        WindowSize pixelSize;
        /** @brief Pixels per logical unit. */
        float contentScale = 1.0f;
    };

    /**
     * @brief A window's close was requested (its close button, or the app asked to quit). With
     * WindowConfig::stopOnMainWindowClose, the main window's request also stops the app; the window itself stays until
     * teardown either way.
     */
    struct WindowCloseRequestedEvent
    {
        WindowId window;
    };
} // namespace Fenrir
