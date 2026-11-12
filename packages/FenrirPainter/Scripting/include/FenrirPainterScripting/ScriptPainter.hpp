#pragma once

// What FenrirPainter's natives take that the painter's own headers cannot share as plain data: a painter and a path a
// script has, named by numbers, and a paint, a stroke style and an image draw with fixed room for what C++ holds in
// vectors and variants. The natives turn these into the painter's own. This header is the painter
// binding's own API description (Scripting/api/FenrirPainterScripting.api.toml), with the painter's plain data it names
// (Transform2D, CornerRadii, Sides, GradientStop, Border, Shadow, NineSlice, TextShadow, GroupFilter and the enums),
// so their C# mirrors and mappings are generated from it and checked against C# as scripts load. The doc comments are
// C#'s.

#include <cstdint>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirAssets/MaterialData.hpp"
#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirPainter/Paint.hpp"
#include "FenrirPainter/Painter.hpp"
#include "FenrirPainter/Path.hpp"

namespace Fenrir::ScriptPainter
{
    /**
     * @brief A painter over the window overlay (Painter.Overlay), which the engine keeps while the C# system that got
     * it runs: a number, so copying it copies no painter. Every Painter call takes one. When the system returns, what
     * it painted is submitted to the overlay and the painter ends; its id is never handed out again, so a later call on
     * it does nothing. The default names none.
     */
    struct PainterId
    {
        /** @brief Which painter: zero for none. */
        std::uint64_t value = 0;
    };

    /**
     * @brief A path a script made (Path2D.Create) and the engine keeps until the script destroys it: subpaths of lines
     * and Bezier curves in UI units, built through its calls and drawn by Painter.FillPath, Painter.StrokePath and
     * Painter.BeginMask. Plain data, so copying it does not copy the path. A destroyed path's calls do nothing; the
     * default names none.
     */
    struct Path2D
    {
        /** @brief Which path: zero for none. */
        std::uint64_t id = 0;
    };

    /** @brief Which paint a Paint is. */
    enum class PaintKind : std::uint8_t
    {
        /** @brief One colour. */
        Solid,
        /** @brief A gradient along the line from Start to End. */
        LinearGradient,
        /** @brief A gradient from Focal out to the circle of Radius about Centre. */
        RadialGradient,
        /** @brief An image repeated over Rect, tinted by Colour. */
        ImagePattern,
    };

    /**
     * @brief How a fill is coloured: a solid colour, a linear or radial gradient of up to eight stops, or an image
     * pattern. Colours have straight alpha. Gradient and pattern coordinates are in the paint's own space, which
     * Transform places in the shape's space. A gradient of more than two stops draws with its first and last: a
     * script's painter has no stop texture.
     */
    struct Paint
    {
        /** @brief Which paint, and so which fields below it reads. */
        PaintKind kind = PaintKind::Solid;
        /** @brief The solid colour, or the image pattern's tint. */
        Colour colour;
        /** @brief A linear gradient's start (offset 0) and end (offset 1). */
        Math::Vec2 start{0.0f, 0.0f};
        Math::Vec2 end{1.0f, 0.0f};
        /** @brief A radial gradient's circle (offset 1) and focal point (offset 0). */
        Math::Vec2 centre{0.0f, 0.0f};
        float radius = 1.0f;
        Math::Vec2 focal{0.0f, 0.0f};
        /** @brief What a gradient does past its ends. */
        GradientSpread spread = GradientSpread::Pad;
        /** @brief How many of Stops a gradient uses, at most eight. */
        std::int32_t stopCount = 0;
        /** @brief A gradient's stops, sorted by offset. */
        GradientStop stops[8];
        /** @brief Places the paint's own space in the shape's. */
        Transform2D transform;
        /** @brief An image pattern's image. */
        AssetHandle<Image> image;
        /** @brief How an image pattern's image is sampled, and what lies outside Rect. */
        SamplerDescription sampler = LinearClamp2D;
        /** @brief Where one copy of an image pattern's image lies. */
        Rect2D rect{.x = 0.0f, .y = 0.0f, .width = 1.0f, .height = 1.0f};
    };

    /**
     * @brief How a path is stroked: its width in UI units (zero is a hairline, one pixel wide), its caps and joins,
     * and up to eight dash lengths, alternating on and off from each subpath's start, DashOffset into the pattern.
     */
    struct StrokeStyle
    {
        float width = 1.0f;
        LineCap cap = LineCap::Butt;
        LineJoin join = LineJoin::Miter;
        /** @brief The longest miter, as a multiple of the width, before a corner is bevelled. */
        float miterLimit = 4.0f;
        /** @brief How many of Dashes the stroke uses; zero is a solid line. */
        std::int32_t dashCount = 0;
        float dashes[8] = {};
        float dashOffset = 0.0f;
    };

    /** @brief An image drawn into a rectangle: the image, its sampler, nine-slice insets when NineSliced, and a tint.
     */
    struct ImageDraw
    {
        AssetHandle<Image> image;
        SamplerDescription sampler = LinearClamp2D;
        /** @brief Whether NineSlice applies: false stretches the image over the rectangle. */
        bool nineSliced = false;
        NineSlice nineSlice;
        Colour tint;
    };
} // namespace Fenrir::ScriptPainter
