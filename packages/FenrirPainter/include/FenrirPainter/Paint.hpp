#pragma once

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirAssets/ImageData.hpp"
#include "FenrirAssets/MaterialData.hpp"
#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    /**
     * @brief A 2D affine transform: where it sends the X axis, the Y axis and the origin. A point `p` goes to
     * `x * p.x + y * p.y + origin`. Default constructed, it is the identity.
     */
    struct Transform2D
    {
        Math::Vec2 x{1.0f, 0.0f};
        Math::Vec2 y{0.0f, 1.0f};
        Math::Vec2 origin{0.0f, 0.0f};

        /** @brief A move by `offset`. */
        [[nodiscard]] static Transform2D Translation(Math::Vec2 offset);
        /** @brief A scale by `factors` about the origin. */
        [[nodiscard]] static Transform2D Scaling(Math::Vec2 factors);
        /** @brief A rotation by `radians`, clockwise on screen (Y is down). */
        [[nodiscard]] static Transform2D Rotation(float radians);

        /** @brief Where `point` goes. */
        [[nodiscard]] Math::Vec2 Apply(Math::Vec2 point) const;
        /** @brief Where the direction `vector` goes, without the translation. */
        [[nodiscard]] Math::Vec2 ApplyVector(Math::Vec2 vector) const;
        /** @brief The transform that undoes this one; the identity if this one has no inverse. */
        [[nodiscard]] Transform2D Inverse() const;
        /** @brief The area scale, the absolute determinant of the 2x2 part. */
        [[nodiscard]] float AreaScale() const;
        /** @brief Whether it keeps axis-aligned rectangles axis-aligned with no flip of either axis. */
        [[nodiscard]] bool IsAxisAligned() const;

        /** @brief `a` after `b`: `(a * b).Apply(p) == a.Apply(b.Apply(p))`. */
        friend Transform2D operator*(const Transform2D& a, const Transform2D& b);
        friend bool operator==(const Transform2D&, const Transform2D&) = default;
    };

    /** @brief A radius for each corner of a rectangle, in UI units; zero is a sharp corner. */
    struct CornerRadii
    {
        float topLeft = 0.0f;
        float topRight = 0.0f;
        float bottomRight = 0.0f;
        float bottomLeft = 0.0f;

        /** @brief `radius` on every corner. */
        [[nodiscard]] static constexpr CornerRadii All(float radius)
        {
            return {radius, radius, radius, radius};
        }

        friend bool operator==(const CornerRadii&, const CornerRadii&) = default;
    };

    /** @brief One value for each side of a rectangle: border widths, nine-slice insets. */
    struct Sides
    {
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
        float left = 0.0f;

        /** @brief `value` on every side. */
        [[nodiscard]] static constexpr Sides All(float value)
        {
            return {value, value, value, value};
        }

        friend bool operator==(const Sides&, const Sides&) = default;
    };

    /** @brief A gradient's colour at one point along it, from 0 at its start to 1 at its end. */
    struct GradientStop
    {
        float offset = 0.0f;
        Colour colour;
    };

    /** @brief What a gradient does past its ends: keeps its end colours, or starts again. */
    enum class GradientSpread : std::uint8_t
    {
        Pad,
        Repeat,
    };

    /**
     * @brief A paint that blends its stops along the line from `start` to `end`, in the paint's own space, which
     * `transform` places in the shape's space. Stops are sorted by offset; more than two need the painter's stop
     * texture.
     */
    struct LinearGradient
    {
        Math::Vec2 start{0.0f};
        Math::Vec2 end{1.0f, 0.0f};
        std::vector<GradientStop> stops;
        GradientSpread spread = GradientSpread::Pad;
        Transform2D transform;
    };

    /**
     * @brief A paint that blends its stops from a focal point (offset 0) out to the circle of `radius` about `centre`
     * (offset 1), in the paint's own space, which `transform` places in the shape's space. With the focal point at the
     * centre it is a plain radial gradient. A focal point outside the circle is drawn just inside it.
     */
    struct RadialGradient
    {
        Math::Vec2 centre{0.0f};
        float radius = 1.0f;
        Math::Vec2 focal{0.0f};
        std::vector<GradientStop> stops;
        GradientSpread spread = GradientSpread::Pad;
        Transform2D transform;
    };

    /**
     * @brief A paint that repeats an image: one copy fills `rect` of the paint's own space, which `transform` places
     * in the shape's space, and the sampler's wrap says what lies outside it. The image's colours are multiplied by
     * `tint`.
     */
    struct ImagePattern
    {
        AssetHandle<Image> image;
        SamplerDescription sampler = LinearClamp2D;
        Rect2D rect{0.0f, 0.0f, 1.0f, 1.0f};
        Colour tint;
        Transform2D transform;
    };

    /**
     * @brief How a fill is coloured: a solid colour, a linear or radial gradient, or an image pattern. Colours are
     * `Colour`s with straight alpha, however they were written; the painter encodes them to sRGB and premultiplies
     * them in the vertex.
     */
    using Paint = std::variant<Colour, LinearGradient, RadialGradient, ImagePattern>;

    /** @brief A border's width and colour for each side, inside the rectangle it is drawn on. */
    struct Border
    {
        Sides widths;
        Colour top;
        Colour right;
        Colour bottom;
        Colour left;

        /** @brief One `width` and `colour` on every side. */
        [[nodiscard]] static Border Uniform(float width, Colour colour)
        {
            return {Sides::All(width), colour, colour, colour, colour};
        }
    };

    /**
     * @brief A box shadow: the rectangle's shape moved by `offset`, grown by `spread` and blurred by `blur` (the CSS
     * blur radius, twice the Gaussian's standard deviation). An outset shadow is drawn outside the rectangle only, an
     * inset one inside it only.
     */
    struct Shadow
    {
        Math::Vec2 offset{0.0f};
        float blur = 0.0f;
        float spread = 0.0f;
        Colour colour = Colour::Black;
        bool inset = false;
    };

    /**
     * @brief Nine-slice insets: the image's corners keep their size, its edges stretch along one axis and its middle
     * along both. `insets` are in the image's pixels, measured on an image of `imageSize` pixels, and drawn at the
     * same size in UI units, scaled down evenly when the rectangle is smaller than they are.
     */
    struct NineSlice
    {
        Sides insets;
        Math::Vec2 imageSize{1.0f};
    };

    /** @brief An image drawn into a rectangle: the image, its sampler, nine-slice insets if any, and a tint. */
    struct ImageDraw
    {
        AssetHandle<Image> image;
        SamplerDescription sampler = LinearClamp2D;
        std::optional<NineSlice> nineSlice;
        Colour tint;
    };
} // namespace Fenrir
