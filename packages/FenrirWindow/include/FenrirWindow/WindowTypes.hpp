#pragma once

#include <cstdint>
#include <string>

#include "FenrirInput/InputTypes.hpp"

// FenrirWindow's neutral types (ADR-0020, ADR-0022). Nothing here names SDL, a graphics API's headers or the OS: the
// windowing library stays inside FenrirWindow's sources. Windows are named by FenrirInput's WindowId, which every input
// event carries.

namespace Fenrir
{
    /** @brief A width and a height: in logical units (a window's size) or in pixels (its backbuffer). */
    struct WindowSize
    {
        int width = 0;
        int height = 0;

        friend constexpr bool operator==(WindowSize a, WindowSize b) noexcept = default;
    };

    /** @brief How Window::Install sets up FenrirWindow and the main window it will make. */
    struct WindowConfig
    {
        /** @brief The main window's title. */
        std::string title = "Fenrir";
        /** @brief The main window's size in logical units; its pixel size is this times the content scale. */
        WindowSize logicalSize{1280, 720};
        /** @brief False makes the main window hidden: for GL tests and offscreen tools. Present on it does nothing. */
        bool visible = true;
        /**
         * @brief True stops the app (App::Stop) when the main window's close is requested. False leaves it to game
         * code, which reads WindowCloseRequestedEvent (to ask "save before quitting?", say).
         */
        bool stopOnMainWindowClose = true;
        /** @brief Seconds slept at the end of PreUpdate while no window can be drawn (all minimised). */
        double minimisedFrameSleep = 0.007;
        /** @brief Seconds slept at the end of PreUpdate while no window has the input focus; 0 never sleeps. */
        double unfocusedFrameSleep = 0.0;
        /** @brief True turns SDL's text input on in each window it makes, so typing sends TextEvent. */
        bool textInputByDefault = true;
    };

    /** @brief How the mouse cursor behaves over a window. */
    enum class CursorMode : std::uint8_t
    {
        /** @brief Shown, and free to leave the window. */
        Normal,
        /** @brief Hidden over the window, and free to leave it. */
        Hidden,
        /** @brief Hidden and held in the window; the mouse reports raw relative motion (for mouse look). */
        Captured,
    };

    /** @brief Which system cursor is shown over a window. `Hidden` shows none. */
    enum class CursorShape : std::uint8_t
    {
        Arrow,
        /** @brief The I-beam, over editable text. */
        Text,
        ResizeEastWest,
        ResizeNorthSouth,
        ResizeNorthWestSouthEast,
        ResizeNorthEastSouthWest,
        /** @brief The pointing hand, over a link or button. */
        Hand,
        NotAllowed,
        Hidden,
    };

    /** @brief The graphics API a graphics surface is made for. Only OpenGL is built (ADR-0016). */
    enum class GraphicsApi : std::uint8_t
    {
        /** @brief An OpenGL 4.6 core context. */
        OpenGL,
    };

    /**
     * @brief The window's own colour buffer, the backbuffer, as the renderer wants it: plain sRGB colour with no depth,
     * stencil or MSAA by default, since every view draws into offscreen targets (ADR-0022).
     */
    struct BackbufferDescription
    {
        bool srgb = true;
        int depthBits = 0;
        int stencilBits = 0;
        /** @brief MSAA samples; 0 for none. */
        int samples = 0;
    };

    /** @brief What the renderer asks a window's graphics surface to be (Window::RequestSurface). */
    struct SurfaceRequest
    {
        GraphicsApi api = GraphicsApi::OpenGL;
        /** @brief A debug context, for debug validation. */
        bool debugContext = false;
        BackbufferDescription backbuffer;
    };
} // namespace Fenrir
