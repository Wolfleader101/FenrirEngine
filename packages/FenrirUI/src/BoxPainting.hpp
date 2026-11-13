#pragma once

#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    class Painter;
    class Paragraph;
} // namespace Fenrir

namespace Fenrir::Detail
{
    struct UIRootState;

    /** @brief `colour` with its alpha multiplied by `alpha`. */
    [[nodiscard]] Colour Faded(Colour colour, float alpha);

    /** @brief Whether two colours are the same in every channel. */
    [[nodiscard]] bool SameColour(const Colour& a, const Colour& b);

    /** @brief Every run of `paragraph` at `point`, in `colour` if given, else its own, faded by `alpha`. */
    void PaintRuns(Painter& painter, const Paragraph& paragraph, Math::Vec2 point, const Colour* colour, float alpha);

    /**
     * @brief Paints the root's laid-out tree into `list` through the painter, in tree order: each element's shadows,
     * background and border, its content, then its children by z-index inside its overflow clip, under its transform
     * and, when its opacity is under one and it has children, inside an offscreen group.
     */
    void PaintRoot(UIRootState& state, DrawList2D& list, float contentScale);
} // namespace Fenrir::Detail
