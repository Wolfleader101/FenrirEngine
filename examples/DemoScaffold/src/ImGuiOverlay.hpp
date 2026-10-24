#pragma once

#include <memory>

namespace Fenrir
{
    class App;
    class AssetStore;
} // namespace Fenrir

namespace FenrirDemo::Detail
{
    /** @brief The ImGuiOverlay's state, shared with its input router. Defined in ImGuiOverlay.cpp. */
    struct ImGuiOverlayState;

    /**
     * @brief The demo scaffold's ImGuiOverlay (ADR-0027, DemoConfig::imguiOverlay): Dear ImGui over the main window,
     * with this class as its platform and renderer backend. Move-only; one at a time, since ImGui's context is global.
     *
     * - **Input:** an input router of priority ImGuiOverlayRouterPriority. It feeds ImGui keys, text, the mouse and the
     *   wheel, consumes what ImGui wants (judged by ImGui's last frame, as ImGui does) and publishes the `pointer`,
     *   `pointerCapture`, `keyboardFocus` and `textInput` claims, with the caret, each frame.
     * - **Drawing:** each frame's ImGui draw data becomes one DrawList2D on `builtin:2d` (the ImGui vertex layout, sRGB
     *   input, straight AlphaBlend, linear blending), submitted to the main window's ImGuiOverlayLayer. ImGui's
     *   textures, the font atlas among them, are ImageData assets owned by the app, replaced whole when ImGui changes
     *   them.
     * - **Platform:** the window's logical size and content scale (fonts are rasterised at the content scale), the
     *   clipboard and the cursor shape through FenrirWindow.
     */
    class ImGuiOverlay
    {
      public:
        /** @brief Makes ImGui's context and registers the router. `store` must outlive the overlay. */
        ImGuiOverlay(Fenrir::App& app, Fenrir::AssetStore& store);
        ImGuiOverlay(ImGuiOverlay&& other) noexcept;
        ImGuiOverlay& operator=(ImGuiOverlay&& other) noexcept;
        ImGuiOverlay(const ImGuiOverlay&) = delete;
        ImGuiOverlay& operator=(const ImGuiOverlay&) = delete;
        /** @brief Removes the router, frees ImGui's textures and destroys its context. */
        ~ImGuiOverlay();

        /**
         * @brief Starts ImGui's frame, after the frame's input is dispatched and before any scene makes ImGui windows,
         * and publishes the input claims ImGui's state now implies.
         */
        void BeginFrame();

        /** @brief Ends ImGui's frame and submits what it drew to the overlay. */
        void EndFrame();

      private:
        std::shared_ptr<ImGuiOverlayState> m_state;
    };
} // namespace FenrirDemo::Detail
