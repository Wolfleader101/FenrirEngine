#pragma once

#include "FenrirUI/Element.hpp"

namespace UIDemo
{
    /**
     * @brief `compass`, a custom element (FenrirUI's Element subclassed in C++): a strip of the headings round the one
     * the camera looks along, painted with the painter as ticks every 15 degrees, taller at the cardinal points, a
     * marker at north and a needle at the centre. Its strokes are its `color`, fading towards its ends.
     */
    class Compass : public Fenrir::Element
    {
      public:
        /** @brief How many degrees of heading the strip spans, end to end. */
        static constexpr float SpanDegrees = 120.0f;

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
    };
} // namespace UIDemo
