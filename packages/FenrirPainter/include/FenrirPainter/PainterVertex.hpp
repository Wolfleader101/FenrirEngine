#pragma once

#include <array>
#include <cstdint>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    /** @brief What a painter quad's fragment stage evaluates (ADR-0060). */
    enum class ShapeKind : std::uint8_t
    {
        Rectangle = 1,
        RoundedRectangle = 2,
        /** @brief The band between a rounded rectangle and its inside, a colour per side. */
        Border = 3,
        /** @brief A blurred rounded rectangle, outside the shape (outset) or inside it (inset). */
        Shadow = 4,
        Circle = 5,
        /** @brief A band of a circle between two angles, with flat ends. */
        Arc = 6,
        /** @brief The band between two circles about one centre. */
        Ring = 7,
        /**
         * @brief A triangle of a tessellated path, fill or stroke: the shader applies only its paint, its clip and
         * the coverage in `local.x`, 1 inside and 0 at the outside of its fringe.
         */
        Triangle = 8,
        /**
         * @brief A glyph's quad over its rectangle of an R8 coverage atlas page, in the image slot: `uv` is in the
         * page's texels, and the shader multiplies the paint by the coverage it samples (ADR-0062's bitmap mode).
         */
        Glyph = 9,
        /**
         * @brief A glyph's quad over its rectangle of an R8 distance-field atlas page, in the image slot, as Glyph's:
         * the shader reconstructs coverage from the distance it samples, `shape.x` being the field's spread in texels,
         * with a one-pixel anti-aliasing width in screen pixels whatever the quad's scale (ADR-0062's SDF mode).
         */
        SdfGlyph = 10,
    };

    /** @brief How a painter quad's pixels are coloured. */
    enum class PaintKind : std::uint8_t
    {
        Solid = 0,
        LinearGradient = 1,
        RadialGradient = 2,
        Image = 3,
    };

    /** @brief The bits of PainterVertex::kinds' third byte. */
    namespace PainterFlags
    {
        /** @brief The gradient repeats past its ends rather than padding. */
        inline constexpr std::uint8_t Repeat = 1u << 0u;
        /** @brief The gradient's colours are a row of the stop texture, not `colour` and `colour2`. */
        inline constexpr std::uint8_t StopRow = 1u << 1u;
        /** @brief The shadow is inset. */
        inline constexpr std::uint8_t Inset = 1u << 2u;
        /** @brief The quad is clipped per pixel by the clip fields. */
        inline constexpr std::uint8_t Clipped = 1u << 3u;
        /**
         * @brief Anti-aliasing is off (PainterConfig::antiAliasing): the shape's and the clip's edges cover a pixel
         * whose centre is inside them fully and any other not at all, rather than fading over one pixel. A shadow's
         * blur stays a blur; a glyph's coverage stays its atlas's.
         */
        inline constexpr std::uint8_t HardEdge = 1u << 4u;
    } // namespace PainterFlags

    /**
     * @brief The painter's fat vertex (ADR-0060): with its quad's three others it carries the whole shape, its paint
     * and its clip, so a panel's boxes batch into one command; a tessellated path's triangles carry their paint and
     * clip the same way. Positions are in the list's local logical units; the
     * other points are in spaces centred on what they describe, mapped per vertex so they interpolate exactly under
     * any affine transform. Colours are RGBA8, red in the lowest byte, sRGB-encoded and premultiplied.
     */
    struct PainterVertex
    {
        /** @brief Where the vertex is, in local logical units. */
        Math::Vec2 position{0.0f};
        /**
         * @brief The vertex in the paint's space, the paint's transform already applied: a linear gradient's offset in
         * `x`, a radial gradient's point with its circle at the origin and radius 1, an image's texture coordinate.
         */
        Math::Vec2 uv{0.0f};
        /**
         * @brief The vertex in the shape's space, the shape's centre at the origin, in UI units; for a Triangle, its
         * coverage in `x`.
         */
        Math::Vec2 local{0.0f};
        /** @brief The vertex in the clip's space, the clip's centre at the origin. */
        Math::Vec2 clipPoint{0.0f};
        /** @brief Half the shape's width and height; for a circle, arc or ring, its outer radius in both. */
        Math::Vec2 halfSize{0.0f};
        /** @brief Half the clip rectangle's width and height. */
        Math::Vec2 clipHalfSize{0.0f};
        /** @brief The shape's corner radii: top-left, top-right, bottom-right, bottom-left. */
        Math::Vec4 radii{0.0f};
        /** @brief The clip's corner radii, in the same order. */
        Math::Vec4 clipRadii{0.0f};
        /**
         * @brief The shape's own numbers: a border's widths (top, right, bottom, left); a shadow's blur, spread and
         * offset; an arc's radius, width, middle angle and half its sweep; a ring's outer and inner radii; an SDF
         * glyph's spread in texels.
         */
        Math::Vec4 shape{0.0f};
        /**
         * @brief The paint's numbers: a radial gradient's focal point in `xy`; the two-stop offsets in `zw`, or the
         * stop texture row's V coordinate in `z`.
         */
        Math::Vec4 extents{0.0f};
        /** @brief The paint's colour: a solid fill, the first stop, an image's tint, a shadow's colour. */
        std::uint32_t colour = 0;
        /** @brief The second stop. */
        std::uint32_t colour2 = 0;
        /** @brief A border's colours: top, right, bottom, left. */
        std::array<std::uint32_t, 4> borderColours{};
        /** @brief Shape kind, paint kind, flags (PainterFlags) and the texture slot an image is sampled from. */
        std::array<std::uint8_t, 4> kinds{};
    };
    static_assert(sizeof(PainterVertex) == 140);

    /** @name The texture slots of a painter command
     * @{
     */
    /** @brief An image or image pattern. */
    inline constexpr std::uint8_t PainterImageSlot = 0;
    /** @brief The stop texture, for gradients of more than two stops. */
    inline constexpr std::uint8_t PainterStopSlot = 1;
    /** @} */

    /**
     * @brief PainterVertex's layout, fifteen attributes in declaration order: position at location 0, uv at 1, local
     * and clipPoint as one `vec4` at 2, halfSize and clipHalfSize as one at 3, radii to extents at 4 to 7, colour and
     * colour2 at 8 and 9, the border colours at 10 to 13 and the kinds at 14. Pairing the `vec2`s keeps it within the
     * sixteen vertex attributes every device has.
     */
    [[nodiscard]] VertexLayout2D PainterVertexLayout();

    /**
     * @brief The painter's 2D pipeline description: `shader` (`builtin:painter`) on PainterVertex's layout with
     * `indexType`, premultiplied alpha, sRGB input, in the root's `blendSpace`.
     */
    [[nodiscard]] PipelineDescription2D PainterPipeline(AssetHandle<Shader> shader, BlendSpace2D blendSpace,
                                                        IndexType2D indexType = IndexType2D::UInt16);
} // namespace Fenrir
