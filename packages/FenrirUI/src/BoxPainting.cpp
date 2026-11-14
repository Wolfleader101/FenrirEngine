#include "BoxPainting.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <variant>
#include <vector>

#include "ElementGeometry.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirPainter/Painter.hpp"
#include "FenrirText/Paragraph.hpp"
#include "FenrirUI/UI.hpp"
#include "Rects.hpp"
#include "Scrolling.hpp"
#include "UIRootState.hpp"

namespace Fenrir::Detail
{
    Colour Faded(Colour colour, float alpha)
    {
        colour.a *= alpha;
        return colour;
    }

    bool SameColour(const Colour& a, const Colour& b)
    {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    }

    void PaintRuns(Painter& painter, const Paragraph& paragraph, Math::Vec2 point, const Colour* colour, float alpha)
    {
        for (const GlyphRun& run : paragraph.Runs())
        {
            GlyphRun tinted = run;
            tinted.colour = Faded(colour != nullptr ? *colour : run.colour, alpha);
            painter.DrawGlyphs(tinted, point);
        }
    }

    namespace
    {
        Rect2D TransformedBounds(const Transform2D& transform, const Rect2D& rect)
        {
            const std::array corners{transform.Apply({rect.x, rect.y}), transform.Apply({rect.x + rect.width, rect.y}),
                                     transform.Apply({rect.x, rect.y + rect.height}),
                                     transform.Apply({rect.x + rect.width, rect.y + rect.height})};
            Math::Vec2 low = corners[0];
            Math::Vec2 high = corners[0];
            for (const Math::Vec2& corner : corners)
            {
                low = glm::min(low, corner);
                high = glm::max(high, corner);
            }
            return {low.x, low.y, high.x - low.x, high.y - low.y};
        }

        class TreePainter
        {
          public:
            TreePainter(UIRootState& state, Painter& painter, AssetStore* store)
                : m_state(state), m_painter(painter), m_store(store), m_groups(state.roots.settings.offscreenGroups)
            {
            }

            void PaintElement(std::uint32_t index, float alpha)
            {
                ElementSlot& slot = m_state.slots[index];
                const ResolvedStyle& style = slot.resolved;
                if (style.display == Display::None || style.opacity <= 0.0f)
                {
                    return;
                }
                ++m_state.stats.elementsPainted;
                const ElementBoxes& boxes = slot.boxes;
                const bool transformed = !style.transform.empty();
                const bool group = m_groups && style.opacity < 1.0f && !slot.children.empty();
                const float ownAlpha = group ? alpha : alpha * style.opacity;

                m_painter.Save();
                if (transformed)
                {
                    m_painter.Concat(ElementTransform(style, boxes.border));
                }
                if (group)
                {
                    m_painter.BeginGroup(LocalBounds(index), style.opacity);
                }
                PaintBackdrop(style, boxes.border);
                PaintBox(style, boxes.border, ownAlpha);
                // Overflow clips the content and the children to the padding box, not the element's own box.
                if (style.overflow != Overflow::Visible)
                {
                    m_painter.ClipRoundedRect(boxes.padding, InnerRadiiOf(style));
                }
                std::vector<ElementId> children = slot.children;
                std::stable_sort(children.begin(), children.end(), [&](ElementId a, ElementId b) {
                    return m_state.slots[a.index].resolved.zIndex < m_state.slots[b.index].resolved.zIndex;
                });
                PaintText(slot, ownAlpha);
                slot.element->Paint(m_painter, ElementPaint{boxes, style, ownAlpha});
                for (const ElementId child : children)
                {
                    PaintElement(child.index, ownAlpha);
                }
                PaintScrollbars(m_state.slots[index], ownAlpha);
                if (group)
                {
                    m_painter.EndGroup();
                }
                m_painter.Restore();
            }

          private:
            /**
             * @brief What the element and everything under it may cover, before its own transform: its border box,
             * its shadows, and its children's bounds (clipped to its padding box when it hides overflow).
             */
            Rect2D LocalBounds(std::uint32_t index) const
            {
                const ElementSlot& slot = m_state.slots[index];
                const ResolvedStyle& style = slot.resolved;
                Rect2D bounds = slot.boxes.border;
                for (const Shadow& shadow : style.boxShadow)
                {
                    const float reach = shadow.spread + shadow.blur;
                    bounds = Union(bounds,
                                   {slot.boxes.border.x + shadow.offset.x - reach,
                                    slot.boxes.border.y + shadow.offset.y - reach,
                                    slot.boxes.border.width + 2.0f * reach, slot.boxes.border.height + 2.0f * reach});
                }
                for (const ElementId child : slot.children)
                {
                    const ElementSlot& childSlot = m_state.slots[child.index];
                    if (childSlot.resolved.display == Display::None)
                    {
                        continue;
                    }
                    Rect2D childBounds = LocalBounds(child.index);
                    if (!childSlot.resolved.transform.empty())
                    {
                        childBounds = TransformedBounds(ElementTransform(childSlot.resolved, childSlot.boxes.border),
                                                        childBounds);
                    }
                    if (style.overflow != Overflow::Visible)
                    {
                        childBounds = Intersect(childBounds, slot.boxes.padding);
                    }
                    bounds = Union(bounds, childBounds);
                }
                return bounds;
            }

            /** @brief A scroll view's bars over its children: each track's box, then its thumb's. */
            void PaintScrollbars(const ElementSlot& slot, float alpha)
            {
                const ScrollbarBoxes bars = ScrollbarsOf(slot);
                if (bars.vertical)
                {
                    PaintBox(slot.scrollbarStyles->track, bars.verticalTrack, alpha);
                    PaintBox(slot.scrollbarStyles->thumb, bars.verticalThumb, alpha);
                }
                if (bars.horizontal)
                {
                    PaintBox(slot.scrollbarStyles->track, bars.horizontalTrack, alpha);
                    PaintBox(slot.scrollbarStyles->thumb, bars.horizontalThumb, alpha);
                }
            }

            /**
             * @brief `backdrop-filter`: what is drawn under the border box blurred, before the element's own boxes.
             * The renderer composites a backdrop as a rectangle and no mask reaches it, so it ignores the corners'
             * radii.
             */
            void PaintBackdrop(const ResolvedStyle& style, const Rect2D& border)
            {
                if (style.backdropBlur > 0.0f)
                {
                    m_painter.BackdropBlur(border, style.backdropBlur);
                }
            }

            /** @brief Shadows (the first on top, so drawn last), background, border and border image. */
            void PaintBox(const ResolvedStyle& style, const Rect2D& border, float alpha)
            {
                const CornerRadii radii = RadiiOf(style);
                for (auto shadow = style.boxShadow.rbegin(); shadow != style.boxShadow.rend(); ++shadow)
                {
                    Shadow faded = *shadow;
                    faded.colour = Faded(faded.colour, alpha);
                    if (faded.colour.a > 0.0f)
                    {
                        m_painter.DrawShadow(border, radii, faded);
                    }
                }
                if (style.backgroundColor.a > 0.0f)
                {
                    m_painter.DrawRoundedRect(border, radii, Faded(style.backgroundColor, alpha));
                }
                PaintBackgroundImage(style, border, radii, alpha);
                const Sides widths{style.borderTopWidth, style.borderRightWidth, style.borderBottomWidth,
                                   style.borderLeftWidth};
                if (widths.top > 0.0f || widths.right > 0.0f || widths.bottom > 0.0f || widths.left > 0.0f)
                {
                    m_painter.DrawBorder(
                        border, radii,
                        Border{widths, Faded(style.borderTopColor, alpha), Faded(style.borderRightColor, alpha),
                               Faded(style.borderBottomColor, alpha), Faded(style.borderLeftColor, alpha)});
                }
                if (const std::optional<Math::Vec2> size = ImageSize(style.borderImageSource))
                {
                    ImageDraw draw;
                    draw.image = style.borderImageSource;
                    draw.nineSlice = NineSlice{style.borderImageSlice, *size};
                    draw.tint = Faded(Colour::White, alpha);
                    m_painter.DrawImage(border, draw);
                }
            }

            void PaintBackgroundImage(const ResolvedStyle& style, const Rect2D& border, const CornerRadii& radii,
                                      float alpha)
            {
                const auto fadedStops = [&](const std::vector<GradientStop>& stops) {
                    std::vector<GradientStop> faded = stops;
                    for (GradientStop& stop : faded)
                    {
                        stop.colour = Faded(stop.colour, alpha);
                    }
                    return faded;
                };
                const Math::Vec2 centre{border.x + border.width / 2.0f, border.y + border.height / 2.0f};
                if (const auto* linear = std::get_if<StyleLinearGradient>(&style.backgroundImage))
                {
                    // CSS's gradient line: through the centre at the angle, long enough to reach the corners.
                    const float radians = linear->degrees * std::numbers::pi_v<float> / 180.0f;
                    const Math::Vec2 direction{std::sin(radians), -std::cos(radians)};
                    const float length = std::abs(border.width * direction.x) + std::abs(border.height * direction.y);
                    LinearGradient paint;
                    paint.start = centre - direction * (length / 2.0f);
                    paint.end = centre + direction * (length / 2.0f);
                    paint.stops = fadedStops(linear->stops);
                    m_painter.DrawRoundedRect(border, radii, paint);
                }
                else if (const auto* radial = std::get_if<StyleRadialGradient>(&style.backgroundImage))
                {
                    // An ellipse reaching the farthest corner: the box's half size times the square root of two.
                    RadialGradient paint;
                    paint.stops = fadedStops(radial->stops);
                    paint.transform = Transform2D::Translation(centre) *
                                      Transform2D::Scaling({border.width / std::numbers::sqrt2_v<float>,
                                                            border.height / std::numbers::sqrt2_v<float>});
                    m_painter.DrawRoundedRect(border, radii, paint);
                }
                else if (const auto* image = std::get_if<AssetHandle<Image>>(&style.backgroundImage))
                {
                    const std::optional<Math::Vec2> size = ImageSize(*image);
                    if (!size)
                    {
                        return;
                    }
                    ImagePattern pattern;
                    pattern.image = *image;
                    pattern.tint = Faded(Colour::White, alpha);
                    pattern.rect = border;
                    if (style.backgroundSize != BackgroundSize::Stretch)
                    {
                        const float fit = style.backgroundSize == BackgroundSize::Cover
                                              ? std::max(border.width / size->x, border.height / size->y)
                                              : std::min(border.width / size->x, border.height / size->y);
                        const Math::Vec2 fitted = *size * fit;
                        pattern.rect = {centre.x - fitted.x / 2.0f, centre.y - fitted.y / 2.0f, fitted.x, fitted.y};
                    }
                    if (style.backgroundSize == BackgroundSize::Contain)
                    {
                        // A contained image does not fill the box: drawn as itself, not as a pattern clamped past it.
                        ImageDraw draw;
                        draw.image = *image;
                        draw.tint = pattern.tint;
                        m_painter.DrawImage(pattern.rect, draw);
                        return;
                    }
                    m_painter.DrawRoundedRect(border, radii, pattern);
                }
            }

            /**
             * @brief The element's paragraph at its content box's top-left: its text shadows first (the first on top,
             * so drawn last), each its runs again moved and in its colour, then the runs in their own.
             */
            void PaintText(const ElementSlot& slot, float alpha)
            {
                if (slot.text == nullptr)
                {
                    return;
                }
                Paragraph& paragraph = slot.text->GetParagraph();
                const Math::Vec2 point{slot.boxes.content.x, slot.boxes.content.y};
                for (auto shadow = slot.resolved.textShadow.rbegin(); shadow != slot.resolved.textShadow.rend();
                     ++shadow)
                {
                    const Colour colour = Faded(shadow->colour, alpha);
                    if (colour.a > 0.0f)
                    {
                        PaintRuns(m_painter, paragraph, point + shadow->offset, &colour, 1.0f);
                    }
                }
                if (alpha >= 1.0f)
                {
                    m_painter.DrawText(paragraph, point);
                }
                else
                {
                    PaintRuns(m_painter, paragraph, point, nullptr, alpha);
                }
            }

            /** @brief The image's size in pixels; none for an empty or stale handle. */
            std::optional<Math::Vec2> ImageSize(AssetHandle<Image> image) const
            {
                if (image.IsEmpty() || m_store == nullptr)
                {
                    return std::nullopt;
                }
                const auto* data = m_store->TryGet(image);
                if (data == nullptr || data->width == 0 || data->height == 0)
                {
                    return std::nullopt;
                }
                return Math::Vec2(static_cast<float>(data->width), static_cast<float>(data->height));
            }

            UIRootState& m_state;
            Painter& m_painter;
            AssetStore* m_store;
            bool m_groups;
        };
    } // namespace

    void PaintRoot(UIRootState& state, DrawList2D& list, float contentScale)
    {
        AssetStore* store = state.app.TryGetResource<AssetStore>();
        if (!state.stops && store != nullptr)
        {
            const AssetHandle<Image> image = store->Add<Image>(StopTexture::Blank(64), AssetOwner::App());
            if (!image.IsEmpty())
            {
                state.stops.emplace(image, 64);
            }
        }

        PainterConfig config;
        config.shader = state.roots.GetPainterShader();
        config.scale = state.scale;
        // The root resolved its blend space from its target when it was made.
        config.blendSpace = *state.config.blendSpace;
        config.snapToPixels = state.config.snapToPixels;
        config.pixelsPerUnit = contentScale;
        config.stops = state.stops ? &*state.stops : nullptr;
        config.fonts = state.roots.GetFonts();
        config.antiAliasing = state.roots.settings.antiAliasing;
        Painter painter(list, config);
        TreePainter(state, painter, store).PaintElement(state.rootElement.index, 1.0f);

        // The stop rows this frame's gradients use reach the image before the list is drawn.
        if (state.stops && store != nullptr)
        {
            const bool used = state.stops->RowCount() > 0;
            if (used || state.stopsUsed)
            {
                store->Replace(state.stops->GetImage(), state.stops->TakeImageData());
            }
            state.stopsUsed = used;
        }
    }
} // namespace Fenrir::Detail
