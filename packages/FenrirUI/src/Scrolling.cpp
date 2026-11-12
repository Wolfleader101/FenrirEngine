#include "Scrolling.hpp"

#include <algorithm>

#include "ElementGeometry.hpp"
#include "UIRootState.hpp"

namespace Fenrir::Detail
{
    namespace
    {
        /** @brief `track` less its style's padding: where the thumb runs. */
        Rect2D Inner(const Rect2D& track, const ResolvedStyle& style)
        {
            const float left = ResolveLength(style.paddingLeft, track.width);
            const float right = ResolveLength(style.paddingRight, track.width);
            const float top = ResolveLength(style.paddingTop, track.width);
            const float bottom = ResolveLength(style.paddingBottom, track.width);
            return {track.x + left, track.y + top, std::max(0.0f, track.width - left - right),
                    std::max(0.0f, track.height - top - bottom)};
        }
    } // namespace

    ScrollbarBoxes ScrollbarsOf(const ElementSlot& slot)
    {
        ScrollbarBoxes bars;
        if (slot.resolved.overflow != Overflow::Scroll || slot.scrollbarStyles == nullptr)
        {
            return bars;
        }
        const ResolvedStyle& track = slot.scrollbarStyles->track;
        const float thickness = ResolveLength(track.width, 0.0f);
        if (thickness <= 0.0f)
        {
            return bars;
        }
        const Rect2D& box = slot.boxes.padding;
        bars.vertical = slot.scrollRange.y > 0.0f;
        bars.horizontal = slot.scrollRange.x > 0.0f;
        // Where both show, the vertical bar stops short of the horizontal one, as a browser's corner does.
        if (bars.vertical)
        {
            bars.verticalTrack = {box.x + box.width - thickness, box.y, thickness,
                                  std::max(0.0f, box.height - (bars.horizontal ? thickness : 0.0f))};
            const Rect2D inner = Inner(bars.verticalTrack, track);
            // The thumb is the track's share the view shows of its content, kept no shorter than it is wide.
            const float shown = box.height / (box.height + slot.scrollRange.y);
            const float length = std::min(inner.height, std::max(inner.height * shown, inner.width));
            bars.travel.y = inner.height - length;
            bars.verticalThumb = {inner.x, inner.y + bars.travel.y * (slot.scrollOffset.y / slot.scrollRange.y),
                                  inner.width, length};
        }
        if (bars.horizontal)
        {
            bars.horizontalTrack = {box.x, box.y + box.height - thickness,
                                    std::max(0.0f, box.width - (bars.vertical ? thickness : 0.0f)), thickness};
            const Rect2D inner = Inner(bars.horizontalTrack, track);
            const float shown = box.width / (box.width + slot.scrollRange.x);
            const float length = std::min(inner.width, std::max(inner.width * shown, inner.height));
            bars.travel.x = inner.width - length;
            bars.horizontalThumb = {inner.x + bars.travel.x * (slot.scrollOffset.x / slot.scrollRange.x), inner.y,
                                    length, inner.height};
        }
        return bars;
    }
} // namespace Fenrir::Detail
