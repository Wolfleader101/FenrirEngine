#include "BoxPainting.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <numbers>
#include <numeric>
#include <optional>
#include <variant>
#include <vector>

#include "ElementGeometry.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirPainter/Painter.hpp"
#include "FenrirPainter/PainterVertex.hpp"
#include "FenrirPainter/StopTexture.hpp"
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

        /**
         * @brief Whether every group and mask `list` opens it closes, in order: a group's end that masks opens a
         * mask's reach, which an EndMask2D closes.
         */
        bool IsBalanced(const DrawList2D& list)
        {
            // What is open, innermost last: true for a mask's reach, false for a group.
            std::vector<bool> open;
            for (const Command2D& command : list.commands)
            {
                if (std::holds_alternative<BeginGroup2D>(command))
                {
                    open.push_back(false);
                }
                else if (const auto* end = std::get_if<EndGroup2D>(&command))
                {
                    if (open.empty() || open.back())
                    {
                        return false;
                    }
                    open.back() = end->mask;
                    if (!end->mask)
                    {
                        open.pop_back();
                    }
                }
                else if (std::holds_alternative<EndMask2D>(command))
                {
                    if (open.empty() || !open.back())
                    {
                        return false;
                    }
                    open.pop_back();
                }
            }
            return open.empty();
        }

        /** @brief An RGBA8 premultiplied colour, every channel multiplied by `alpha`. */
        std::uint32_t FadedRgba8(std::uint32_t colour, float alpha)
        {
            std::uint32_t faded = 0;
            for (std::uint32_t shift = 0; shift < 32; shift += 8)
            {
                const auto channel = static_cast<float>((colour >> shift) & 0xFFu);
                faded |= static_cast<std::uint32_t>(std::lround(channel * alpha)) << shift;
            }
            return faded;
        }

        /**
         * @brief Fades every painter vertex of `list` by `alpha`: its colours are premultiplied, so every channel of
         * each is multiplied, as a box's colours are faded. A gradient's stop rows and an image's texels are faded
         * through the colours that tint them.
         */
        void Fade(DrawList2D& list, float alpha)
        {
            std::vector<PainterVertex> vertices(list.vertices.size() / sizeof(PainterVertex));
            std::memcpy(vertices.data(), list.vertices.data(), vertices.size() * sizeof(PainterVertex));
            for (PainterVertex& vertex : vertices)
            {
                vertex.colour = FadedRgba8(vertex.colour, alpha);
                vertex.colour2 = FadedRgba8(vertex.colour2, alpha);
                for (std::uint32_t& border : vertex.borderColours)
                {
                    border = FadedRgba8(border, alpha);
                }
            }
            std::memcpy(list.vertices.data(), vertices.data(), vertices.size() * sizeof(PainterVertex));
        }

        class TreePainter
        {
          public:
            TreePainter(UIRootState& state, Painter& painter, DrawList2D& list, AssetStore* store)
                : m_state(state), m_painter(painter), m_list(list), m_store(store),
                  m_groups(state.roots.settings.offscreenGroups)
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
                PaintStaged(ElementId{index, slot.version}, ownAlpha);
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
             * @brief What the paint stage painted into the element, appended to the list as it was recorded (its
             * painter already drew under the element's transform and clips), its colours faded by `alpha` as the
             * element's own content is. A list that leaves a group or a mask open would unbalance the root's list,
             * which the renderer then skips whole, so it is dropped, logged.
             */
            void PaintStaged(ElementId element, float alpha)
            {
                for (const std::unique_ptr<StagePaint>& staged : m_state.stagePaints)
                {
                    if (staged->element != element)
                    {
                        continue;
                    }
                    if (!IsBalanced(staged->list))
                    {
                        m_state.app.Logger()->Warn(
                            "FenrirUI: what the paint stage painted into an element left a group or a mask open, so it "
                            "is not drawn");
                        return;
                    }
                    if (alpha >= 1.0f)
                    {
                        AppendList(m_list, staged->list);
                        return;
                    }
                    DrawList2D faded = staged->list;
                    Fade(faded, alpha);
                    AppendList(m_list, faded);
                    return;
                }
            }

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
            DrawList2D& m_list;
            AssetStore* m_store;
            bool m_groups;
        };
    } // namespace

    namespace
    {
        /** @brief The root's stop texture, made on the first frame that paints with an asset store. */
        StopTexture* StopsOf(UIRootState& state, AssetStore* store)
        {
            if (!state.stops && store != nullptr)
            {
                const AssetHandle<Image> image = store->Add<Image>(StopTexture::Blank(64), AssetOwner::App());
                if (!image.IsEmpty())
                {
                    state.stops.emplace(image, 64);
                }
            }
            return state.stops ? &*state.stops : nullptr;
        }

        /** @brief What the root paints with this frame: PaintRoot's painter and the paint stage's are made with it. */
        PainterConfig RootPainterConfig(UIRootState& state, AssetStore* store, float contentScale)
        {
            PainterConfig config;
            config.shader = state.roots.GetPainterShader();
            config.scale = state.scale;
            // The root resolved its blend space from its target when it was made.
            config.blendSpace = *state.config.blendSpace;
            config.snapToPixels = state.config.snapToPixels;
            config.pixelsPerUnit = contentScale;
            config.stops = StopsOf(state, store);
            config.fonts = state.roots.GetFonts();
            config.antiAliasing = state.roots.settings.antiAliasing;
            return config;
        }

        /** @brief `size` rounded up to a multiple of `multiple`. */
        std::size_t RoundUp(std::size_t size, std::size_t multiple)
        {
            return (size + multiple - 1) / multiple * multiple;
        }

        /** @brief The bytes a vertex of `pipeline` takes. */
        std::size_t StrideOf(const PipelineDescription2D& pipeline)
        {
            return std::max<std::size_t>(pipeline.vertexLayout.stride, 1);
        }

        /** @brief The bytes an index of `pipeline` takes. */
        std::size_t IndexSizeOf(const PipelineDescription2D& pipeline)
        {
            return pipeline.indexType == IndexType2D::UInt16 ? 2 : 4;
        }
    } // namespace

    void PaintRoot(UIRootState& state, DrawList2D& list, float contentScale)
    {
        AssetStore* store = state.app.TryGetResource<AssetStore>();
        Painter painter(list, RootPainterConfig(state, store, contentScale));
        TreePainter(state, painter, list, store).PaintElement(state.rootElement.index, 1.0f);

        // The stop rows this frame's gradients use (the paint stage's among them) reach the image before the list is
        // drawn.
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

    StagePaint& OpenStagePaint(UIRootState& state, std::uint32_t index)
    {
        auto staged = std::make_unique<StagePaint>();
        staged->element = ElementId{index, state.slots[index].version};
        AssetStore* store = state.app.TryGetResource<AssetStore>();
        Painter& painter = staged->painter.emplace(staged->list, RootPainterConfig(state, store, state.ContentScale()));

        // The element's ancestors from the root down, then the element: what PaintElement applies before its content.
        std::vector<std::uint32_t> chain;
        for (ElementId at = staged->element; at.IsValid(); at = state.slots[at.index].parent)
        {
            chain.push_back(at.index);
        }
        for (auto at = chain.rbegin(); at != chain.rend(); ++at)
        {
            const ElementSlot& slot = state.slots[*at];
            if (!slot.resolved.transform.empty())
            {
                painter.Concat(ElementTransform(slot.resolved, slot.boxes.border));
            }
            if (slot.resolved.overflow != Overflow::Visible)
            {
                painter.ClipRoundedRect(slot.boxes.padding, InnerRadiiOf(slot.resolved));
            }
        }
        state.stagePaints.push_back(std::move(staged));
        return *state.stagePaints.back();
    }

    void AppendList(DrawList2D& into, const DrawList2D& from)
    {
        // Every stride and index size the appended commands read at, so that each one's base vertex and index offset
        // are whole numbers of them in the joined bytes.
        std::size_t vertexAlign = 1;
        std::size_t indexAlign = 1;
        for (const PipelineDescription2D& pipeline : from.pipelines)
        {
            vertexAlign = std::lcm(vertexAlign, StrideOf(pipeline));
            indexAlign = std::lcm(indexAlign, IndexSizeOf(pipeline));
        }
        into.vertices.resize(RoundUp(into.vertices.size(), vertexAlign));
        into.indices.resize(RoundUp(into.indices.size(), indexAlign));
        const std::size_t vertexBytes = into.vertices.size();
        const std::size_t indexBytes = into.indices.size();
        into.vertices.insert(into.vertices.end(), from.vertices.begin(), from.vertices.end());
        into.indices.insert(into.indices.end(), from.indices.begin(), from.indices.end());

        std::vector<std::uint32_t> pipelines;
        pipelines.reserve(from.pipelines.size());
        for (const PipelineDescription2D& pipeline : from.pipelines)
        {
            pipelines.push_back(into.AddPipeline(pipeline));
        }
        for (Command2D command : from.commands)
        {
            if (auto* draw = std::get_if<DrawCommand2D>(&command))
            {
                const PipelineDescription2D& pipeline = from.pipelines[draw->pipeline];
                draw->pipeline = pipelines[draw->pipeline];
                draw->baseVertex += static_cast<std::int32_t>(vertexBytes / StrideOf(pipeline));
                draw->indexOffset += static_cast<std::uint32_t>(indexBytes / IndexSizeOf(pipeline));
            }
            into.commands.push_back(std::move(command));
        }
    }
} // namespace Fenrir::Detail
