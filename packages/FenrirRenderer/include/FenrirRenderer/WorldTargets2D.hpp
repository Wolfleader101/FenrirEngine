#pragma once

#include <mutex>
#include <utility>
#include <vector>

#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    namespace Detail
    {
        /** @brief How the renderer takes the world-space 2D targets' submissions. Defined in FenrirRenderer's sources.
         */
        struct WorldTargetSubmissions;
    } // namespace Detail

    /**
     * @brief A quad in the world that 2D draw lists are drawn on (ADR-0060's world-space 2D target): a component on an
     * entity of the active scene, whose world transform places it. The game owns that transform like any other.
     *
     * The quad lies in the entity's XY plane, centred on its origin, `size` world units across (X) and up (Y), and
     * faces +Z; it is drawn from both sides. A list's local coordinates are logical units from the quad's top-left
     * corner, X right and Y down, `unitsPerWorldUnit` of them to a world unit, so a list `size * unitsPerWorldUnit`
     * units across covers it. Nothing outside the quad is cut away: a list that draws past its edge draws past it.
     */
    struct WorldQuad2D
    {
        /** @brief The quad's width and height in world units. */
        Math::Vec2 size{1.0f, 1.0f};
        /** @brief The list's logical units to one world unit, above zero: 100 makes a unit a centimetre. */
        float unitsPerWorldUnit = 100.0f;

        /** @brief Whether the quad can be drawn: a size above zero both ways and units above zero. */
        [[nodiscard]] bool IsDrawable() const noexcept
        {
            return size.x > 0.0f && size.y > 0.0f && unitsPerWorldUnit > 0.0f;
        }

        /** @brief The quad's size in the list's logical units. */
        [[nodiscard]] Math::Vec2 LogicalSize() const noexcept
        {
            return size * unitsPerWorldUnit;
        }

        /** @brief From the list's logical units (top-left origin, Y down) to the entity's space (centred, Y up). */
        [[nodiscard]] Math::Mat4 LocalToEntity() const noexcept
        {
            Math::Mat4 matrix(1.0f);
            matrix[0][0] = 1.0f / unitsPerWorldUnit;
            matrix[1][1] = -1.0f / unitsPerWorldUnit;
            matrix[3] = Math::Vec4(-size.x * 0.5f, size.y * 0.5f, 0.0f, 1.0f);
            return matrix;
        }

        /** @brief From a point in the entity's space, on the quad's plane, to the list's logical units. */
        [[nodiscard]] Math::Vec2 EntityToLocal(Math::Vec3 point) const noexcept
        {
            return {(point.x + size.x * 0.5f) * unitsPerWorldUnit, (size.y * 0.5f - point.y) * unitsPerWorldUnit};
        }
    };

    /**
     * @brief The world-space 2D target (ADR-0060): an app resource Renderer::Install adds, through which 2D draw lists
     * are drawn on WorldQuad2D entities in the scene, as ImageTargets2D draws them into images. Submit lists to an
     * entity at any time before `Render`; the renderer draws them that frame and drops them after it. A submission to
     * an entity that is not valid in the active scene, or has no WorldQuad2D, is dropped and logged once per entity.
     *
     * **Drawing.** Every camera's view draws each quad in its `Transparent` pass, depth tested against the opaque items
     * without writing depth, sorted back to front with the transparent items by the view depth of the entity's origin;
     * the quad's lists draw in submission order, each list's commands in order, as one item. A quad whose corners are
     * all outside one side of a view, or all behind its camera, is culled from that view. Local coordinates reach clip
     * space through the quad's world matrix and the view (set 0's `localToClip`, `Fenrir/2D.glsl`), at a content scale
     * of 1, so `pixelSize` is the quad's size in logical units.
     *
     * **Clips and blending.** There is no hardware scissor: a command's scissor is ignored, and clips are the
     * painter's, realised per pixel in its shader from each vertex's clip data (ADR-0060), so only a list's innermost
     * clip cuts what it draws. The scene's colour is linear and floating point: the `Gamma` blend space is logged and
     * draws as `Linear`. Lighting does not reach the lists (the spec's out of scope).
     *
     * **Groups** are drawn into transient images at one pixel per logical unit, bounded by the quad, and composited on
     * it; a backdrop filter on the quad itself (not inside a group) has no target to read, so it is logged once and
     * its group draws without one. A list sampling an image 2D target reads what the image held before this frame.
     *
     * Submit may be called from any system, parallel ones included.
     */
    class WorldTargets2D
    {
      public:
        WorldTargets2D() = default;
        WorldTargets2D(const WorldTargets2D&) = delete;
        WorldTargets2D& operator=(const WorldTargets2D&) = delete;
        WorldTargets2D(WorldTargets2D&&) = delete;
        WorldTargets2D& operator=(WorldTargets2D&&) = delete;
        ~WorldTargets2D() = default;

        /** @brief Draws `list` on `entity`'s WorldQuad2D in the next `Render`, after the lists submitted to it before.
         */
        void Submit(Entity entity, DrawList2D list)
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_submissions.push_back({entity, std::move(list)});
        }

      private:
        friend struct Detail::WorldTargetSubmissions;

        /** @brief One Submit call's arguments. */
        struct Submission
        {
            Entity entity;
            DrawList2D list;
        };

        std::mutex m_mutex;
        std::vector<Submission> m_submissions;
    };
} // namespace Fenrir
