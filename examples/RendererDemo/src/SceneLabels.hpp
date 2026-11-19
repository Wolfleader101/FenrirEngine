#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirPainter/Painter.hpp"
#include "FenrirRenderer/Overlay.hpp"
#include "FenrirText/Paragraph.hpp"

namespace Fenrir
{
    class App;
    class FontLibrary;
} // namespace Fenrir

namespace RendererDemo
{
    /**
     * @brief A scene's 2D labels in FenrirText (debug screen text's replacement in Overlay and Painter), painted into a
     * list of their own on a window's overlay: each from its top-left in the overlay's logical units, white, in the
     * embedded default face at debug screen text's 16 units, blended in gamma space as a UI on the window is.
     *
     * Each frame: Begin, Draw each label, then Submit. A paragraph is kept per label drawn in a frame and laid out
     * again only when its text or the window's content scale changes. Without the app's FontLibrary (FenrirUI's), the
     * labels draw nothing. Move it only between frames, never between Begin and Submit.
     */
    class SceneLabels
    {
      public:
        /**
         * @brief Starts a frame's labels on `window`, painted with `painterShader` (builtin:painter), at the window's
         * content scale.
         */
        void Begin(Fenrir::App& app, Fenrir::WindowId window, Fenrir::AssetHandle<Fenrir::Shader> painterShader);

        /** @brief Draws `text` with its top-left at `topLeft`; nothing outside Begin and Submit. */
        void Draw(std::string_view text, Fenrir::Math::Vec2 topLeft);

        /** @brief Submits the frame's labels to `window`'s overlay on `layer`, after the lists submitted there so far.
         */
        void Submit(Fenrir::Overlay& overlay, std::int32_t layer);

      private:
        /** @brief A label's paragraph, and the content scale it was laid out at. */
        struct Label
        {
            Fenrir::Paragraph paragraph;
            float laidOutAt = 0.0f;
        };

        std::vector<Label> m_labels;
        std::size_t m_next = 0;
        Fenrir::DrawList2D m_list;
        std::optional<Fenrir::Painter> m_painter;
        Fenrir::WindowId m_window{};
        Fenrir::FontLibrary* m_fonts = nullptr;
        float m_pixelsPerUnit = 1.0f;
    };
} // namespace RendererDemo
