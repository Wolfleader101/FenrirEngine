#include "FenrirPainter/Painter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <span>
#include <type_traits>
#include <variant>

#include "Colours.hpp"
#include "FenrirPainter/StopTexture.hpp"
#include "Radii.hpp"
#include "Tessellation.hpp"

namespace Fenrir
{
    namespace
    {
        using PainterDetail::FitRadii;

        /** @brief The most vertices one command of 16-bit indices reaches: indices 0 to 65535. */
        constexpr std::uint32_t MaxVertices16 = 65536;
        constexpr float TwoPi = 2.0f * std::numbers::pi_v<float>;

        /** @brief An image paint in the vertex: its kind, the slot it is sampled from, and its tint. */
        void SetImagePaint(PainterVertex& vertex, const Colour& tint)
        {
            vertex.kinds[1] = static_cast<std::uint8_t>(PaintKind::Image);
            vertex.kinds[3] = PainterImageSlot;
            vertex.colour = PainterDetail::PackColour(tint);
        }

        Math::Vec2 Centre(const Rect2D& rect)
        {
            return {rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f};
        }

        Math::Vec2 Half(const Rect2D& rect)
        {
            return {rect.width * 0.5f, rect.height * 0.5f};
        }

        /** @brief The axis-aligned bounds of `rect` under `transform`. */
        Rect2D Bounds(const Rect2D& rect, const Transform2D& transform)
        {
            const std::array<Math::Vec2, 4> corners{transform.Apply({rect.x, rect.y}),
                                                    transform.Apply({rect.x + rect.width, rect.y}),
                                                    transform.Apply({rect.x + rect.width, rect.y + rect.height}),
                                                    transform.Apply({rect.x, rect.y + rect.height})};
            Math::Vec2 min = corners[0];
            Math::Vec2 max = corners[0];
            for (const Math::Vec2 corner : corners)
            {
                min = {std::min(min.x, corner.x), std::min(min.y, corner.y)};
                max = {std::max(max.x, corner.x), std::max(max.y, corner.y)};
            }
            return {min.x, min.y, max.x - min.x, max.y - min.y};
        }

        Rect2D Intersect(const Rect2D& a, const Rect2D& b)
        {
            const float x0 = std::max(a.x, b.x);
            const float y0 = std::max(a.y, b.y);
            const float x1 = std::min(a.x + a.width, b.x + b.width);
            const float y1 = std::min(a.y + a.height, b.y + b.height);
            return {x0, y0, std::max(x1 - x0, 0.0f), std::max(y1 - y0, 0.0f)};
        }

        /** @brief The stops sorted by offset, which gradients assume. */
        std::vector<GradientStop> Sorted(const std::vector<GradientStop>& stops)
        {
            std::vector<GradientStop> sorted = stops;
            std::ranges::stable_sort(sorted, {}, &GradientStop::offset);
            return sorted;
        }

        /**
         * @brief A gradient's stops in the vertex: the two ends in `colour` and `colour2` with their offsets, or a row
         * of the stop texture when there are more and it has room. False when there is nothing to draw.
         */
        bool SetStops(PainterVertex& vertex, const std::vector<GradientStop>& stops, GradientSpread spread,
                      StopTexture* texture, std::optional<TextureSlot2D>& stopsSlot)
        {
            if (stops.empty())
            {
                return false;
            }
            const std::vector<GradientStop> sorted = Sorted(stops);
            if (spread == GradientSpread::Repeat)
            {
                vertex.kinds[2] |= PainterFlags::Repeat;
            }
            if (sorted.size() > 2 && texture != nullptr)
            {
                if (const std::optional<float> row = texture->AddRow(sorted))
                {
                    vertex.kinds[2] |= PainterFlags::StopRow;
                    vertex.extents.z = *row;
                    stopsSlot = TextureSlot2D{texture->GetImage(), LinearClamp2D};
                    return true;
                }
            }
            vertex.colour = PainterDetail::PackColour(sorted.front().colour);
            vertex.colour2 = PainterDetail::PackColour(sorted.back().colour);
            vertex.extents.z = sorted.front().offset;
            vertex.extents.w = sorted.back().offset;
            return true;
        }

        /** @brief Appends `indices`, each moved on by `first`, as `T`s. */
        template <typename T>
        void AppendIndices(DrawList2D& list, std::span<const std::uint32_t> indices, std::uint32_t first)
        {
            std::vector<T> converted(indices.size());
            for (std::size_t i = 0; i < indices.size(); ++i)
            {
                converted[i] = static_cast<T>(indices[i] + first);
            }
            list.AppendIndices(std::span<const T>(converted));
        }

        /** @brief A quad's two triangles over its corners 0 to 3. */
        constexpr std::array<std::uint32_t, 6> QuadIndices{0, 1, 2, 0, 2, 3};

        /** @brief Whether `style` dashes: some dash length is positive. */
        bool Dashed(const StrokeStyle& style)
        {
            return std::ranges::any_of(style.dashes, [](float length) { return length > 0.0f; });
        }
    } // namespace

    Painter::Painter(DrawList2D& list, const PainterConfig& config) : m_list(list), m_config(config)
    {
        m_pipeline = m_list.AddPipeline(PainterPipeline(config.shader, config.blendSpace, config.indexType));
    }

    void Painter::Save()
    {
        m_saved.push_back(m_state);
    }

    void Painter::Restore()
    {
        if (!m_saved.empty())
        {
            m_state = m_saved.back();
            m_saved.pop_back();
        }
    }

    void Painter::Translate(Math::Vec2 offset)
    {
        Concat(Transform2D::Translation(offset));
    }

    void Painter::Scale(Math::Vec2 factors)
    {
        Concat(Transform2D::Scaling(factors));
    }

    void Painter::Rotate(float radians)
    {
        Concat(Transform2D::Rotation(radians));
    }

    void Painter::Concat(const Transform2D& transform)
    {
        m_state.transform = m_state.transform * transform;
    }

    const Transform2D& Painter::GetTransform() const noexcept
    {
        return m_state.transform;
    }

    void Painter::ClipRect(const Rect2D& rect)
    {
        SetClip(rect, {});
    }

    void Painter::ClipRoundedRect(const Rect2D& rect, const CornerRadii& radii)
    {
        SetClip(rect, radii);
    }

    void Painter::SetClip(const Rect2D& rect, const CornerRadii& radii)
    {
        const Transform2D toLogical = ToLogical();
        const Rect2D bounds = Bounds(rect, toLogical);
        m_state.scissor = m_state.scissor ? Intersect(*m_state.scissor, bounds) : bounds;

        const Math::Vec4 fitted = FitRadii(rect, radii);
        Clip clip;
        if (toLogical.IsAxisAligned() && fitted == Math::Vec4(0.0f))
        {
            // A sharp axis-aligned rectangle is kept in logical units, so a later one can meet it exactly.
            Rect2D logical = bounds;
            if (m_state.clip && m_state.clip->logicalRect)
            {
                logical = Intersect(*m_state.clip->logicalRect, logical);
            }
            clip.toClip = Transform2D::Translation(-Centre(logical));
            clip.halfSize = Half(logical);
            clip.logicalRect = logical;
        }
        else
        {
            clip.toClip = Transform2D::Translation(-Centre(rect)) * toLogical.Inverse();
            clip.halfSize = Half(rect);
            clip.radii = fitted;
        }
        m_state.clip = clip;
    }

    Transform2D Painter::ToLogical() const
    {
        return Transform2D::Scaling({m_config.scale, m_config.scale}) * m_state.transform;
    }

    float Painter::Margin() const
    {
        // One logical unit, a pixel or more, in the shape's space.
        const float scale = std::sqrt(ToLogical().AreaScale());
        return scale > 0.0f ? 1.0f / scale : 0.0f;
    }

    Rect2D Painter::Snap(const Rect2D& rect) const
    {
        const Transform2D toLogical = ToLogical();
        if (!m_config.snapToPixels || m_config.pixelsPerUnit <= 0.0f || !toLogical.IsAxisAligned())
        {
            return rect;
        }
        const float pixels = m_config.pixelsPerUnit;
        const auto snap = [pixels](Math::Vec2 point) {
            return Math::Vec2(std::round(point.x * pixels), std::round(point.y * pixels)) / pixels;
        };
        const Transform2D back = toLogical.Inverse();
        const Math::Vec2 min = back.Apply(snap(toLogical.Apply({rect.x, rect.y})));
        const Math::Vec2 max = back.Apply(snap(toLogical.Apply({rect.x + rect.width, rect.y + rect.height})));
        return {min.x, min.y, max.x - min.x, max.y - min.y};
    }

    std::uint32_t Painter::Begin(const PaintSetup& paint, std::uint32_t vertices, std::uint32_t indices)
    {
        const std::uint32_t stride = sizeof(PainterVertex);
        const std::size_t indexSize = m_config.indexType == IndexType2D::UInt16 ? 2 : 4;
        const std::array<std::pair<std::size_t, std::optional<TextureSlot2D>>, 2> wanted{
            {{PainterImageSlot, paint.image}, {PainterStopSlot, paint.stops}}};

        // Only a draw this painter made, still the list's last, is extended: never past a group's begin or end.
        const DrawCommand2D* last = m_command && *m_command + 1 == m_list.commands.size()
                                        ? std::get_if<DrawCommand2D>(&m_list.commands[*m_command])
                                        : nullptr;
        bool extend = last != nullptr;
        if (extend)
        {
            const DrawCommand2D& command = *last;
            extend = command.scissor == m_state.scissor;
            if (m_config.indexType == IndexType2D::UInt16 && m_commandVertices + vertices > MaxVertices16)
            {
                extend = false;
            }
            for (const auto& [slot, texture] : wanted)
            {
                // A slot no draw of the command has filled may be filled; a filled one must match.
                if (texture && slot < command.textures.size() && !command.textures[slot].image.IsEmpty() &&
                    command.textures[slot] != *texture)
                {
                    extend = false;
                }
            }
        }

        if (!extend)
        {
            m_list.vertices.resize((m_list.vertices.size() + stride - 1) / stride * stride);
            m_list.indices.resize((m_list.indices.size() + indexSize - 1) / indexSize * indexSize);
            auto& command = std::get<DrawCommand2D>(m_list.commands.emplace_back(DrawCommand2D{}));
            command.pipeline = m_pipeline;
            command.scissor = m_state.scissor;
            command.indexOffset = static_cast<std::uint32_t>(m_list.indices.size() / indexSize);
            command.baseVertex = static_cast<std::int32_t>(m_list.vertices.size() / stride);
            m_command = m_list.commands.size() - 1;
            m_commandVertices = 0;
        }

        auto& command = std::get<DrawCommand2D>(m_list.commands[*m_command]);
        for (const auto& [slot, texture] : wanted)
        {
            if (texture)
            {
                if (command.textures.size() <= slot)
                {
                    command.textures.resize(slot + 1);
                }
                command.textures[slot] = *texture;
            }
        }
        const std::uint32_t first = m_commandVertices;
        m_commandVertices += vertices;
        command.indexCount += indices;
        return first;
    }

    void Painter::Emit(const Quad& quad, const PaintSetup& paint)
    {
        if (m_state.scissor && (m_state.scissor->width <= 0.0f || m_state.scissor->height <= 0.0f))
        {
            return;
        }
        const Transform2D toLogical = ToLogical();
        PainterVertex vertex = quad.vertex;
        if (m_state.clip)
        {
            vertex.kinds[2] |= PainterFlags::Clipped;
            vertex.clipHalfSize = m_state.clip->halfSize;
            vertex.clipRadii = m_state.clip->radii;
        }

        const std::uint32_t first = Begin(paint, 4, 6);
        const std::array<Math::Vec2, 4> corners{quad.min, Math::Vec2(quad.max.x, quad.min.y), quad.max,
                                                Math::Vec2(quad.min.x, quad.max.y)};
        std::array<PainterVertex, 4> vertices{};
        for (std::size_t i = 0; i < 4; ++i)
        {
            const Math::Vec2 point = quad.centre + corners[i];
            vertices[i] = vertex;
            vertices[i].local = corners[i];
            vertices[i].position = toLogical.Apply(point);
            vertices[i].uv = paint.toPaint.Apply(point);
            if (m_state.clip)
            {
                vertices[i].clipPoint = m_state.clip->toClip.Apply(vertices[i].position);
            }
        }
        m_list.AppendVertices(std::span<const PainterVertex>(vertices));
        if (m_config.indexType == IndexType2D::UInt16)
        {
            AppendIndices<std::uint16_t>(m_list, QuadIndices, first);
        }
        else
        {
            AppendIndices<std::uint32_t>(m_list, QuadIndices, first);
        }
    }

    bool Painter::SetPaint(PainterVertex& vertex, PaintSetup& setup, const Paint& paint) const
    {
        bool draw = true;
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, Colour>)
                {
                    vertex.kinds[1] = static_cast<std::uint8_t>(PaintKind::Solid);
                    vertex.colour = PainterDetail::PackColour(value);
                }
                else if constexpr (std::is_same_v<T, LinearGradient>)
                {
                    vertex.kinds[1] = static_cast<std::uint8_t>(PaintKind::LinearGradient);
                    const Math::Vec2 d = value.end - value.start;
                    const float length2 = Math::Dot(d, d);
                    // The offset along the gradient, t = dot(q - start, d) / |d|^2, as a transform into uv.x.
                    Transform2D along;
                    if (length2 > 0.0f)
                    {
                        along.x = {d.x / length2, 0.0f};
                        along.y = {d.y / length2, 0.0f};
                        along.origin = {-Math::Dot(value.start, d) / length2, 0.0f};
                    }
                    setup.toPaint = along * value.transform.Inverse();
                    draw = SetStops(vertex, value.stops, value.spread, m_config.stops, setup.stops);
                }
                else if constexpr (std::is_same_v<T, RadialGradient>)
                {
                    vertex.kinds[1] = static_cast<std::uint8_t>(PaintKind::RadialGradient);
                    const float radius = std::max(value.radius, 1e-6f);
                    setup.toPaint = Transform2D::Scaling({1.0f / radius, 1.0f / radius}) *
                                    Transform2D::Translation(-value.centre) * value.transform.Inverse();
                    // A focal point on or outside the circle has no cone; it is drawn just inside.
                    Math::Vec2 focal = (value.focal - value.centre) / radius;
                    const float distance = std::sqrt(Math::Dot(focal, focal));
                    if (distance > 0.99f)
                    {
                        focal *= 0.99f / distance;
                    }
                    vertex.extents.x = focal.x;
                    vertex.extents.y = focal.y;
                    draw = SetStops(vertex, value.stops, value.spread, m_config.stops, setup.stops);
                }
                else
                {
                    SetImagePaint(vertex, value.tint);
                    const Rect2D& rect = value.rect;
                    const Math::Vec2 size{rect.width != 0.0f ? rect.width : 1.0f,
                                          rect.height != 0.0f ? rect.height : 1.0f};
                    setup.toPaint = Transform2D::Scaling(Math::Vec2(1.0f) / size) *
                                    Transform2D::Translation({-rect.x, -rect.y}) * value.transform.Inverse();
                    setup.image = TextureSlot2D{value.image, value.sampler};
                }
            },
            paint);
        return draw;
    }

    void Painter::DrawShape(Quad quad, const Paint& paint)
    {
        PaintSetup setup;
        if (SetPaint(quad.vertex, setup, paint))
        {
            Emit(quad, setup);
        }
    }

    Painter::Quad Painter::Box(const Rect2D& rect, ShapeKind kind) const
    {
        const float margin = Margin();
        Quad quad;
        quad.centre = Centre(rect);
        quad.vertex.halfSize = Half(rect);
        quad.min = -quad.vertex.halfSize - Math::Vec2(margin);
        quad.max = quad.vertex.halfSize + Math::Vec2(margin);
        quad.vertex.kinds[0] = static_cast<std::uint8_t>(kind);
        return quad;
    }

    Painter::Quad Painter::Square(Math::Vec2 centre, float radius, ShapeKind kind) const
    {
        return Box({centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f}, kind);
    }

    void Painter::DrawRect(const Rect2D& rect, const Paint& paint)
    {
        DrawShape(Box(Snap(rect), ShapeKind::Rectangle), paint);
    }

    void Painter::DrawRoundedRect(const Rect2D& rect, const CornerRadii& radii, const Paint& paint)
    {
        const Rect2D snapped = Snap(rect);
        Quad quad = Box(snapped, ShapeKind::RoundedRectangle);
        quad.vertex.radii = FitRadii(snapped, radii);
        DrawShape(quad, paint);
    }

    void Painter::DrawBorder(const Rect2D& rect, const CornerRadii& radii, const Border& border)
    {
        const Rect2D snapped = Snap(rect);
        Quad quad = Box(snapped, ShapeKind::Border);
        quad.vertex.radii = FitRadii(snapped, radii);
        quad.vertex.shape = {border.widths.top, border.widths.right, border.widths.bottom, border.widths.left};
        quad.vertex.borderColours = {PainterDetail::PackColour(border.top), PainterDetail::PackColour(border.right),
                                     PainterDetail::PackColour(border.bottom), PainterDetail::PackColour(border.left)};
        Emit(quad, {});
    }

    void Painter::DrawShadow(const Rect2D& rect, const CornerRadii& radii, const Shadow& shadow)
    {
        Quad quad = Box(rect, ShapeKind::Shadow);
        quad.vertex.radii = FitRadii(rect, radii);
        quad.vertex.shape = {shadow.blur, shadow.spread, shadow.offset.x, shadow.offset.y};
        quad.vertex.colour = PainterDetail::PackColour(shadow.colour);
        if (shadow.inset)
        {
            quad.vertex.kinds[2] |= PainterFlags::Inset;
        }
        else
        {
            // The blur is a Gaussian of standard deviation blur / 2, covered three times over.
            const float margin = Margin();
            const Math::Vec2 reach =
                quad.vertex.halfSize + Math::Vec2(shadow.spread + 1.5f * std::max(shadow.blur, 0.0f));
            quad.min = shadow.offset - reach - Math::Vec2(margin);
            quad.max = shadow.offset + reach + Math::Vec2(margin);
        }
        Emit(quad, {});
    }

    void Painter::DrawCircle(Math::Vec2 centre, float radius, const Paint& paint)
    {
        if (radius <= 0.0f)
        {
            return;
        }
        DrawShape(Square(centre, radius, ShapeKind::Circle), paint);
    }

    void Painter::DrawArc(Math::Vec2 centre, float radius, float startRadians, float sweepRadians, float width,
                          const Paint& paint)
    {
        const float half = width * 0.5f;
        if (std::abs(sweepRadians) >= TwoPi)
        {
            DrawRing(centre, radius + half, std::max(radius - half, 0.0f), paint);
            return;
        }
        if (sweepRadians < 0.0f)
        {
            startRadians += sweepRadians;
            sweepRadians = -sweepRadians;
        }
        const float outer = radius + half;
        if (outer <= 0.0f || width <= 0.0f || sweepRadians == 0.0f)
        {
            return;
        }
        Quad quad = Square(centre, outer, ShapeKind::Arc);
        quad.vertex.shape = {radius, width, startRadians + sweepRadians * 0.5f, sweepRadians * 0.5f};
        DrawShape(quad, paint);
    }

    void Painter::DrawRing(Math::Vec2 centre, float outerRadius, float innerRadius, const Paint& paint)
    {
        if (outerRadius <= 0.0f || innerRadius >= outerRadius)
        {
            return;
        }
        Quad quad = Square(centre, outerRadius, ShapeKind::Ring);
        quad.vertex.shape = {outerRadius, std::max(innerRadius, 0.0f), 0.0f, 0.0f};
        DrawShape(quad, paint);
    }

    void Painter::DrawImage(const Rect2D& rect, const ImageDraw& image)
    {
        const Rect2D dest = Snap(rect);
        if (!image.nineSlice)
        {
            ImagePattern pattern;
            pattern.image = image.image;
            pattern.sampler = image.sampler;
            pattern.rect = dest;
            pattern.tint = image.tint;
            DrawShape(Box(dest, ShapeKind::Rectangle), pattern);
            return;
        }

        const NineSlice& slice = *image.nineSlice;
        const Sides& insets = slice.insets;
        // Insets wider than the rectangle are scaled down together.
        float factor = 1.0f;
        if (insets.left + insets.right > dest.width && insets.left + insets.right > 0.0f)
        {
            factor = std::min(factor, dest.width / (insets.left + insets.right));
        }
        if (insets.top + insets.bottom > dest.height && insets.top + insets.bottom > 0.0f)
        {
            factor = std::min(factor, dest.height / (insets.top + insets.bottom));
        }
        const float x1 = dest.x + dest.width;
        const float y1 = dest.y + dest.height;
        const std::array<float, 4> xs{dest.x, dest.x + insets.left * factor, x1 - insets.right * factor, x1};
        const std::array<float, 4> ys{dest.y, dest.y + insets.top * factor, y1 - insets.bottom * factor, y1};
        const Math::Vec2 size{std::max(slice.imageSize.x, 1.0f), std::max(slice.imageSize.y, 1.0f)};
        const std::array<float, 4> us{0.0f, insets.left / size.x, 1.0f - insets.right / size.x, 1.0f};
        const std::array<float, 4> vs{0.0f, insets.top / size.y, 1.0f - insets.bottom / size.y, 1.0f};
        const float margin = Margin();
        const std::optional<TextureSlot2D> texture = TextureSlot2D{image.image, image.sampler};

        for (std::size_t row = 0; row < 3; ++row)
        {
            for (std::size_t column = 0; column < 3; ++column)
            {
                const float width = xs[column + 1] - xs[column];
                const float height = ys[row + 1] - ys[row];
                if (width <= 0.0f || height <= 0.0f)
                {
                    continue;
                }
                // Only the image's outer edges are anti-aliased. At a seam the quad ends exactly, so its neighbour
                // meets it with no overlap, and the shape runs on past it so the seam's pixels are fully covered.
                const float left = column > 0 ? margin : 0.0f;
                const float right = column < 2 ? margin : 0.0f;
                const float top = row > 0 ? margin : 0.0f;
                const float bottom = row < 2 ? margin : 0.0f;
                const Rect2D shape{xs[column] - left, ys[row] - top, width + left + right, height + top + bottom};
                Quad quad = Box(shape, ShapeKind::Rectangle);
                const Math::Vec2 min{xs[column] - (margin - left), ys[row] - (margin - top)};
                const Math::Vec2 max{xs[column + 1] + (margin - right), ys[row + 1] + (margin - bottom)};
                quad.min = min - quad.centre;
                quad.max = max - quad.centre;
                SetImagePaint(quad.vertex, image.tint);

                const float su = (us[column + 1] - us[column]) / width;
                const float sv = (vs[row + 1] - vs[row]) / height;
                PaintSetup piece;
                piece.toPaint.x = {su, 0.0f};
                piece.toPaint.y = {0.0f, sv};
                piece.toPaint.origin = {us[column] - xs[column] * su, vs[row] - ys[row] * sv};
                piece.image = texture;
                Emit(quad, piece);
            }
        }
    }

    float Painter::LogicalTolerance() const
    {
        const float pixels = m_config.pixelsPerUnit > 0.0f ? m_config.pixelsPerUnit : 1.0f;
        return std::max(m_config.tolerance, 1e-3f) / pixels;
    }

    void Painter::FillPath(const Path& path, const Paint& paint)
    {
        const Transform2D toLogical = ToLogical();
        if (toLogical.AreaScale() <= 0.0f)
        {
            return;
        }
        FillContours(PainterDetail::Flatten(path, toLogical, LogicalTolerance()), path.GetFillRule(), paint);
    }

    void Painter::StrokePath(const Path& path, const StrokeStyle& style, const Paint& paint)
    {
        if (StrokeShape(path, style, paint))
        {
            return;
        }
        const Transform2D toLogical = ToLogical();
        const float area = toLogical.AreaScale();
        if (area <= 0.0f)
        {
            return;
        }
        const float tolerance = LogicalTolerance();
        if (style.width <= 0.0f)
        {
            // A hairline is stroked after the transform, one pixel wide, its dashes scaled with the path.
            StrokeStyle hairline = style;
            hairline.width = 1.0f / (m_config.pixelsPerUnit > 0.0f ? m_config.pixelsPerUnit : 1.0f);
            const float scale = std::sqrt(area);
            for (float& length : hairline.dashes)
            {
                length *= scale;
            }
            hairline.dashOffset *= scale;
            const std::vector<PainterDetail::Contour> contours = PainterDetail::Flatten(path, toLogical, tolerance);
            FillContours(PainterDetail::StrokeContours(contours, hairline, tolerance), FillRule::NonZero, paint);
            return;
        }
        // Stroked in UI units, so the transform scales and shears the width as it does the path.
        FillPath(path.StrokeOutline(style, tolerance / std::sqrt(area)), paint);
    }

    bool Painter::StrokeShape(const Path& path, const StrokeStyle& style, const Paint& paint)
    {
        if (!path.m_shape || !(style.width > 0.0f) || Dashed(style))
        {
            return false;
        }
        const Path::Shape& shape = *path.m_shape;
        const float half = style.width * 0.5f;
        if (shape.kind == Path::Shape::Kind::Circle)
        {
            const float radius = shape.rect.width * 0.5f;
            const Math::Vec2 centre{shape.rect.x + radius, shape.rect.y + radius};
            DrawRing(centre, radius + half, std::max(radius - half, 0.0f), paint);
            return true;
        }
        const Colour* colour = std::get_if<Colour>(&paint);
        if (colour == nullptr)
        {
            return false;
        }
        // A sharp corner's outer edge is the border's only when it miters; a right angle's miter is sqrt(2) widths.
        const std::array<float, 4> radii{shape.radii.topLeft, shape.radii.topRight, shape.radii.bottomRight,
                                         shape.radii.bottomLeft};
        const bool miters = style.join == LineJoin::Miter && style.miterLimit >= std::numbers::sqrt2_v<float>;
        if (!miters && std::ranges::any_of(radii, [](float radius) { return radius <= 0.0f; }))
        {
            return false;
        }
        const auto grown = [half](float radius) { return radius > 0.0f ? radius + half : 0.0f; };
        const Rect2D& rect = shape.rect;
        DrawBorder({rect.x - half, rect.y - half, rect.width + style.width, rect.height + style.width},
                   {grown(radii[0]), grown(radii[1]), grown(radii[2]), grown(radii[3])},
                   Border::Uniform(style.width, *colour));
        return true;
    }

    void Painter::FillContours(const std::vector<PainterDetail::Contour>& contours, FillRule rule, const Paint& paint)
    {
        if (m_state.scissor && (m_state.scissor->width <= 0.0f || m_state.scissor->height <= 0.0f))
        {
            return;
        }
        const float pixels = m_config.pixelsPerUnit > 0.0f ? m_config.pixelsPerUnit : 1.0f;
        const PainterDetail::Tessellation tessellation = PainterDetail::Tessellate(contours, rule, 1.0f / pixels);
        if (tessellation.indices.empty())
        {
            return;
        }

        PainterVertex vertex;
        vertex.kinds[0] = static_cast<std::uint8_t>(ShapeKind::Triangle);
        PaintSetup setup;
        if (!SetPaint(vertex, setup, paint))
        {
            return;
        }
        if (m_state.clip)
        {
            vertex.kinds[2] |= PainterFlags::Clipped;
            vertex.clipHalfSize = m_state.clip->halfSize;
            vertex.clipRadii = m_state.clip->radii;
        }
        // The paint is placed in UI units, as a shape's is.
        const Transform2D toUi = ToLogical().Inverse();
        std::vector<PainterVertex> vertices(tessellation.positions.size(), vertex);
        for (std::size_t i = 0; i < vertices.size(); ++i)
        {
            vertices[i].position = tessellation.positions[i];
            vertices[i].uv = setup.toPaint.Apply(toUi.Apply(tessellation.positions[i]));
            vertices[i].local = {tessellation.coverage[i], 0.0f};
            if (m_state.clip)
            {
                vertices[i].clipPoint = m_state.clip->toClip.Apply(tessellation.positions[i]);
            }
        }

        const auto append = [this, &setup](std::span<const PainterVertex> chunk,
                                           std::span<const std::uint32_t> indices) {
            const std::uint32_t first =
                Begin(setup, static_cast<std::uint32_t>(chunk.size()), static_cast<std::uint32_t>(indices.size()));
            m_list.AppendVertices(chunk);
            if (m_config.indexType == IndexType2D::UInt16)
            {
                AppendIndices<std::uint16_t>(m_list, indices, first);
            }
            else
            {
                AppendIndices<std::uint32_t>(m_list, indices, first);
            }
        };
        if (m_config.indexType != IndexType2D::UInt16 || vertices.size() <= MaxVertices16)
        {
            append(vertices, tessellation.indices);
            return;
        }
        // Too many vertices for one command of 16-bit indices: commands of whole triangles, each with its own vertices.
        constexpr std::uint32_t Unused = std::numeric_limits<std::uint32_t>::max();
        std::vector<std::uint32_t> remap(vertices.size(), Unused);
        std::vector<PainterVertex> chunk;
        std::vector<std::uint32_t> indices;
        const auto flush = [&]() {
            if (!indices.empty())
            {
                append(chunk, indices);
            }
            std::ranges::fill(remap, Unused);
            chunk.clear();
            indices.clear();
        };
        for (std::size_t t = 0; t + 2 < tessellation.indices.size(); t += 3)
        {
            if (chunk.size() + 3 > MaxVertices16)
            {
                flush();
            }
            for (std::size_t k = 0; k < 3; ++k)
            {
                const std::uint32_t source = tessellation.indices[t + k];
                if (remap[source] == Unused)
                {
                    remap[source] = static_cast<std::uint32_t>(chunk.size());
                    chunk.push_back(vertices[source]);
                }
                indices.push_back(remap[source]);
            }
        }
        flush();
    }
} // namespace Fenrir
