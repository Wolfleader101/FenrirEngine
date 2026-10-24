#pragma once

#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirInput/InputTypes.hpp"

namespace Fenrir
{
    namespace Detail
    {
        /** @brief How the renderer takes the overlay's submissions. Defined in FenrirRenderer's sources. */
        struct OverlaySubmissions;
    } // namespace Detail

    /**
     * @brief The window overlay (ADR-0027): an app resource Renderer::Install adds, through which any UI draws over the
     * game. Submit 2D draw lists to a window on an overlay layer at any time before `Render`; the renderer draws them
     * that frame and clears them after it.
     *
     * **Order:** layers draw lowest first; within a layer, submissions in the order they were made, and each list's
     * commands in its order. Debug screen text and on-screen messages draw above every layer.
     *
     * **The target:** each window's `Overlay` stage, after every view's `Output`, orthographic over the window's
     * logical size (local coordinates: logical units from its top-left, Y down, as the mouse's), with no depth and no
     * culling. A scissor is scaled by the window's content scale, rounded outwards to whole pixels and clamped to the
     * window. Blending is linear and the sRGB backbuffer encodes on write; a pipeline may opt into the `Gamma` blend
     * space.
     *
     * A window with no submissions draws no UI. A submission to a window the renderer does not draw (anything but the
     * main window, for now) is dropped and logged once per window. Submit may be called from any system, parallel ones
     * included.
     */
    class Overlay
    {
      public:
        Overlay() = default;
        Overlay(const Overlay&) = delete;
        Overlay& operator=(const Overlay&) = delete;
        Overlay(Overlay&&) = delete;
        Overlay& operator=(Overlay&&) = delete;
        ~Overlay() = default;

        /** @brief Draws `list` over `window` on overlay layer `layer` in the next `Render`. */
        void Submit(WindowId window, std::int32_t layer, DrawList2D list)
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_submissions.push_back({window, layer, std::move(list)});
        }

      private:
        friend struct Detail::OverlaySubmissions;

        /** @brief One Submit call's arguments. */
        struct Submission
        {
            WindowId window;
            std::int32_t layer = 0;
            DrawList2D list;
        };

        std::mutex m_mutex;
        std::vector<Submission> m_submissions;
    };
} // namespace Fenrir
