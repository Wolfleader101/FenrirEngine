#pragma once

#include <array>

#include "FenrirText/Paragraph.hpp"
#include "FenrirUI/Element.hpp"

namespace UIDemo
{
    /**
     * @brief `compass`, a custom element (FenrirUI's Element subclassed in C++): a strip of the headings round the one
     * the camera looks along, painted with the painter as ticks every 15 degrees, taller at the cardinal points, each
     * cardinal point's letter over its tick, and a needle under the centre. Its strokes and letters are its `color`,
     * fading towards its ends. The letters are paragraphs in SdfFamily, the embedded face in SDF mode (ADR-0062).
     */
    class Compass : public Fenrir::Element
    {
      public:
        /** @brief How many degrees of heading the strip spans, end to end. */
        static constexpr float SpanDegrees = 120.0f;
        /** @brief The cardinal letters' size, in UI units. */
        static constexpr float LabelSize = 15.0f;

        /** @brief Sets the heading at its centre, in degrees clockwise from north. */
        void SetHeading(float degrees) noexcept
        {
            m_heading = degrees;
        }

        /** @brief The heading at its centre. */
        [[nodiscard]] float GetHeading() const noexcept
        {
            return m_heading;
        }

        void Paint(Fenrir::Painter& painter, const Fenrir::ElementPaint& paint) override;

      private:
        float m_heading = 0.0f;
        /** @brief The letters of north, east, south and west, laid out again as their size or fade changes. */
        std::array<Fenrir::Paragraph, 4> m_labels;
    };
} // namespace UIDemo
