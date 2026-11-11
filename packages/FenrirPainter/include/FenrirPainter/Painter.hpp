#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirPainter/Paint.hpp"
#include "FenrirPainter/PainterVertex.hpp"

namespace Fenrir
{
    class StopTexture;

    /** @brief What a Painter draws with and into: its shader, its root's scale and blend space, and its options. */
    struct PainterConfig
    {
        /** @brief `builtin:painter`, from Painting::GetShader or a load of that name. */
        AssetHandle<Shader> shader;
        /** @brief The root scale: logical units per UI unit. */
        float scale = 1.0f;
        /**
         * @brief The root's blend space: `Gamma` for a UI root on the window overlay, `Linear` for one in an image or
         * the world (ADR-0065).
         */
        BlendSpace2D blendSpace = BlendSpace2D::Linear;
        /** @brief Snap the edges of rectangles drawn under an axis-aligned transform to whole pixels. */
        bool snapToPixels = false;
        /** @brief The target's pixels per logical unit (its content scale), which snapping rounds to. */
        float pixelsPerUnit = 1.0f;
        /**
         * @brief The index width. With 16-bit indices a command past 65535 vertices starts another with a base vertex;
         * 32-bit indices never need to.
         */
        IndexType2D indexType = IndexType2D::UInt16;
        /** @brief Where gradients of more than two stops get their rows; without one they use their end stops. */
        StopTexture* stops = nullptr;
    };

    /**
     * @brief The painter (ADR-0060): a value over one 2D draw list that appends shapes, each one quad of PainterVertex
     * evaluated in the painter's shader, with a paint per call, under a save/restore stack of transform and clip. It
     * keeps nothing between frames: make one over each frame's list.
     *
     * Coordinates are UI units, multiplied by the root scale into the list's logical units. Consecutive draws with the
     * same texture slots and scissor extend one command; a texture in a slot another draw of the command filled, or a
     * scissor change, starts another. The painter appends to the list's vertex and index bytes after whatever is there,
     * aligned to its vertex and index sizes, and only ever extends a command it made last.
     */
    class Painter
    {
      public:
        Painter(DrawList2D& list, const PainterConfig& config);

        /** @name State
         * Save pushes the transform and clip, Restore pops them; a Restore with nothing saved does nothing.
         * @{
         */
        void Save();
        void Restore();
        /** @} */

        /** @name Transform
         * Each applies before the current transform, in UI units, as a canvas does.
         * @{
         */
        void Translate(Math::Vec2 offset);
        void Scale(Math::Vec2 factors);
        /** @brief Clockwise on screen (Y is down). */
        void Rotate(float radians);
        void Concat(const Transform2D& transform);
        [[nodiscard]] const Transform2D& GetTransform() const noexcept;
        /** @} */

        /** @name Clip
         * A clip under the current transform, intersected with the clips already set. The hardware scissor is the
         * intersection of the clip stack's bounds; the innermost clip is evaluated per pixel with its radii and
         * rotation, and a rectangle clip that meets an axis-aligned rectangle clip is their exact intersection.
         * Outside the scissor nothing is appended.
         * @{
         */
        void ClipRect(const Rect2D& rect);
        void ClipRoundedRect(const Rect2D& rect, const CornerRadii& radii);
        /** @} */

        /** @name Draws
         * Each appends one quad (a nine-slice image, nine), anti-aliased over one pixel by the shader.
         * @{
         */
        void DrawRect(const Rect2D& rect, const Paint& paint);
        /** @brief Radii larger than the rectangle allows are scaled down together, as CSS does. */
        void DrawRoundedRect(const Rect2D& rect, const CornerRadii& radii, const Paint& paint);
        /** @brief The border inside `rect`, each side its own width and colour, split at the corners' diagonals. */
        void DrawBorder(const Rect2D& rect, const CornerRadii& radii, const Border& border);
        /** @brief The box shadow of the rounded rectangle `rect`. */
        void DrawShadow(const Rect2D& rect, const CornerRadii& radii, const Shadow& shadow);
        void DrawCircle(Math::Vec2 centre, float radius, const Paint& paint);
        /**
         * @brief A band `width` wide centred on the circle of `radius`, from `startRadians` clockwise through
         * `sweepRadians`, with flat ends; a sweep of a whole turn or more is a ring.
         */
        void DrawArc(Math::Vec2 centre, float radius, float startRadians, float sweepRadians, float width,
                     const Paint& paint);
        /** @brief The band between the circles of `outerRadius` and `innerRadius` about `centre`. */
        void DrawRing(Math::Vec2 centre, float outerRadius, float innerRadius, const Paint& paint);
        /** @brief The image stretched over `rect`, or nine-sliced into it. */
        void DrawImage(const Rect2D& rect, const ImageDraw& image);
        /** @} */

      private:
        struct Clip
        {
            /** @brief From local logical units to the clip's space, its centre at the origin. */
            Transform2D toClip;
            Math::Vec2 halfSize{0.0f};
            Math::Vec4 radii{0.0f};
            /** @brief The clip in logical units, when it is an axis-aligned rectangle with sharp corners. */
            std::optional<Rect2D> logicalRect;
        };

        struct State
        {
            Transform2D transform;
            std::optional<Clip> clip;
            /** @brief The intersection of the clip stack's bounds, in logical units. */
            std::optional<Rect2D> scissor;
        };

        /**
         * @brief One quad's shape before its paint: the shape's centre in UI units under the current transform, the
         * quad's corners about it, and the vertex fields every corner shares.
         */
        struct Quad
        {
            Math::Vec2 centre{0.0f};
            Math::Vec2 min{0.0f};
            Math::Vec2 max{0.0f};
            PainterVertex vertex;
        };

        /** @brief The current transform followed by the root scale: UI units to logical units. */
        [[nodiscard]] Transform2D ToLogical() const;
        /** @brief One logical unit in the shape's space: how far a quad reaches past its shape for anti-aliasing. */
        [[nodiscard]] float Margin() const;
        /** @brief A quad of `kind` over `rect`, reaching the margin past it. */
        [[nodiscard]] Quad Box(const Rect2D& rect, ShapeKind kind) const;
        void SetClip(const Rect2D& rect, const CornerRadii& radii);
        [[nodiscard]] Rect2D Snap(const Rect2D& rect) const;
        /** @brief Sets the quad's paint and emits it, unless the paint has nothing to draw. */
        void DrawShape(Quad quad, const Paint& paint);
        void Emit(const Quad& quad, const Transform2D& toPaint, std::optional<TextureSlot2D> image,
                  std::optional<TextureSlot2D> stops);
        /**
         * @brief Makes room for one quad needing `image` and `stops` in the command it extends or starts; returns the
         * quad's first vertex counted from the command's base vertex.
         */
        [[nodiscard]] std::uint32_t BeginQuad(std::optional<TextureSlot2D> image, std::optional<TextureSlot2D> stops);

        DrawList2D& m_list;
        PainterConfig m_config;
        std::uint32_t m_pipeline = 0;
        State m_state;
        std::vector<State> m_saved;
        /** @brief The command this painter made last, if it is still the list's last. */
        std::optional<std::size_t> m_command;
        std::uint32_t m_commandVertices = 0;
    };
} // namespace Fenrir
