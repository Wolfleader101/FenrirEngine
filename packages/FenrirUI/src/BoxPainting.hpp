#pragma once

#include <cstdint>

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
    struct StagePaint;
    struct UIRootState;

    /** @brief `colour` with its alpha multiplied by `alpha`. */
    [[nodiscard]] Colour Faded(Colour colour, float alpha);

    /** @brief Whether two colours are the same in every channel. */
    [[nodiscard]] bool SameColour(const Colour& a, const Colour& b);

    /** @brief Every run of `paragraph` at `point`, in `colour` if given, else its own, faded by `alpha`. */
    void PaintRuns(Painter& painter, const Paragraph& paragraph, Math::Vec2 point, const Colour* colour, float alpha);

    /**
     * @brief Paints the root's laid-out tree into `list` through the painter, in tree order: each element's shadows,
     * background and border, its content, what the paint stage painted into it (UIRootState::stagePaints), then its
     * children by z-index inside its overflow clip, under its transform and, when its opacity is under one and it has
     * children, inside an offscreen group.
     */
    void PaintRoot(UIRootState& state, DrawList2D& list, float contentScale);

    /**
     * @brief The painter for the paint stage to paint into the element at `index` (UIRoot::PaintInto): over its own
     * list, configured as PaintRoot's painter is, with the transforms and overflow clips PaintRoot draws the element's
     * content under (its ancestors' and its own) already applied.
     */
    [[nodiscard]] StagePaint& OpenStagePaint(UIRootState& state, std::uint32_t index);

    /**
     * @brief Appends `from`'s commands to `into`, after what is there: its vertex and index bytes after `into`'s
     * (padded to every stride and index size `from`'s commands use), its pipelines added to `into`'s table, and each
     * command's pipeline, index offset and base vertex moved to match. Groups and masks keep their order.
     */
    void AppendList(DrawList2D& into, const DrawList2D& from);
} // namespace Fenrir::Detail
