#include "SceneLabels.hpp"

#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirText/FontLibrary.hpp"
#include "FenrirText/TextStyle.hpp"
#include "FenrirWindow/Window.hpp"

namespace RendererDemo
{
    namespace
    {
        // Debug screen text's size, which the labels replace.
        constexpr float LabelSize = 16.0f;
    } // namespace

    void SceneLabels::Begin(Fenrir::App& app, Fenrir::WindowId window,
                            Fenrir::AssetHandle<Fenrir::Shader> painterShader)
    {
        m_next = 0;
        m_window = window;
        m_fonts = app.TryGetResource<Fenrir::FontLibrary>();
        m_pixelsPerUnit = Fenrir::Window::ContentScale(app, window);
        m_list = Fenrir::DrawList2D{};
        Fenrir::PainterConfig config;
        config.shader = painterShader;
        config.blendSpace = Fenrir::BlendSpace2D::Gamma;
        m_painter.emplace(m_list, config);
    }

    void SceneLabels::Draw(std::string_view text, Fenrir::Math::Vec2 topLeft)
    {
        if (!m_painter || m_fonts == nullptr || m_pixelsPerUnit <= 0.0f)
        {
            return;
        }
        if (m_next == m_labels.size())
        {
            m_labels.emplace_back();
        }
        Label& label = m_labels.at(m_next);
        ++m_next;
        // A paragraph lays out in the target's pixels (the painter's text), which the painter divides back to units.
        if (label.paragraph.Text() != text || label.laidOutAt != m_pixelsPerUnit)
        {
            Fenrir::TextStyle style;
            style.size = LabelSize * m_pixelsPerUnit;
            style.colour = Fenrir::Colour::White;
            label.paragraph.SetText(text, style);
            label.paragraph.Layout(*m_fonts, Fenrir::UnlimitedWidth);
            label.laidOutAt = m_pixelsPerUnit;
        }
        m_painter->DrawText(label.paragraph, topLeft);
    }

    void SceneLabels::Submit(Fenrir::Overlay& overlay, std::int32_t layer)
    {
        if (!m_painter)
        {
            return;
        }
        // The painter appends to the list, so it goes before the list does.
        m_painter.reset();
        overlay.Submit(m_window, layer, std::move(m_list));
        m_list = Fenrir::DrawList2D{};
    }
} // namespace RendererDemo
