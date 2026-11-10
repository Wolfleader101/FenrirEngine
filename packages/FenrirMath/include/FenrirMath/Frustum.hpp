#pragma once

#include <array>
#include <cstddef>
#include <span>

#include <glm/glm.hpp>

#include "Math.hpp"

namespace Fenrir::Math
{
    /**
     * @brief The volume a view sees, as the planes that bound it in world space (ADR-0057): what the renderer culls
     * against, public so a game culls or streams its own chunks and cells by the same maths. Each plane is a `Vec4`
     * whose xyz is its unit normal pointing into the volume and whose w is its distance, so a point `p` is on the
     * inside of it when `dot(xyz, p) + w >= 0`.
     */
    class Frustum
    {
      public:
        /** @brief No planes, so everything is inside. */
        Frustum() = default;

        /**
         * @brief The planes of a view whose clip space is `viewProjection`, a reversed-Z projection with depth in
         * [0, 1] (ADR-0026) times a view matrix: left, right, bottom, top, near, then far. A perspective projection
         * with an infinite far plane has no far plane, so five planes; an orthographic one has six.
         */
        [[nodiscard]] static Frustum FromViewProjection(const Mat4& viewProjection);

        /** @brief Its planes, in FromViewProjection's order. */
        [[nodiscard]] std::span<const Vec4> Planes() const noexcept
        {
            return std::span<const Vec4>(m_planes).first(m_count);
        }

        /** @brief Whether any of `sphere` is inside every plane: false only when it is wholly outside one. */
        [[nodiscard]] bool Intersects(const Sphere& sphere) const noexcept;

        /**
         * @brief Whether `box`, in world space, may be seen: false only when its eight corners are all outside one
         * plane. A box that is outside the volume but straddles two planes at a corner is kept.
         */
        [[nodiscard]] bool Intersects(const AABB& box) const noexcept;

        /**
         * @brief Whether `box` under `transform` may be seen: its eight corners are moved by `transform` and the box
         * is dropped only when every one is outside one plane. The test the cull shader repeats (ADR-0057).
         */
        [[nodiscard]] bool Intersects(const AABB& box, const Mat4& transform) const noexcept;

        /**
         * @brief A sphere around `box` under `transform`: the box's centre moved by `transform`, and its half-diagonal
         * scaled by the largest of `transform`'s axis scales. It encloses every moved corner, loosely for a long thin
         * box, and is the cheap first test before the box's.
         */
        [[nodiscard]] static Sphere SphereOf(const AABB& box, const Mat4& transform);

      private:
        std::array<Vec4, 6> m_planes{};
        std::size_t m_count = 0;
    };
} // namespace Fenrir::Math
