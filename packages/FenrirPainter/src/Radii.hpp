#pragma once

#include <algorithm>

#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirPainter/Paint.hpp"

namespace Fenrir::PainterDetail
{
    /** @brief `radii` with none negative, scaled down together until adjacent ones fit each side, as CSS does. */
    inline Math::Vec4 FitRadii(const Rect2D& rect, const CornerRadii& radii)
    {
        const Math::Vec4 r{std::max(radii.topLeft, 0.0f), std::max(radii.topRight, 0.0f),
                           std::max(radii.bottomRight, 0.0f), std::max(radii.bottomLeft, 0.0f)};
        float factor = 1.0f;
        const auto fit = [&factor](float side, float sum) {
            if (sum > side && sum > 0.0f)
            {
                factor = std::min(factor, std::max(side, 0.0f) / sum);
            }
        };
        fit(rect.width, r.x + r.y);
        fit(rect.width, r.w + r.z);
        fit(rect.height, r.x + r.w);
        fit(rect.height, r.y + r.z);
        // A circular corner reaches at most half the shorter side, or the shader's box would bulge.
        const float most = std::max(std::min(rect.width, rect.height) * 0.5f, 0.0f);
        const Math::Vec4 scaled = r * factor;
        return {std::min(scaled.x, most), std::min(scaled.y, most), std::min(scaled.z, most), std::min(scaled.w, most)};
    }
} // namespace Fenrir::PainterDetail
