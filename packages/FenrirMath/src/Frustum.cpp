#include "FenrirMath/Frustum.hpp"

#include <algorithm>
#include <cmath>

namespace Fenrir::Math
{
    std::array<Vec3, 8> Corners(const AABB& box)
    {
        std::array<Vec3, 8> corners;
        for (std::size_t corner = 0; corner < corners.size(); ++corner)
        {
            corners[corner] = Vec3((corner & 1) != 0 ? box.max.x : box.min.x, (corner & 2) != 0 ? box.max.y : box.min.y,
                                   (corner & 4) != 0 ? box.max.z : box.min.z);
        }
        return corners;
    }

    AABB Union(const AABB& a, const AABB& b)
    {
        return {glm::min(a.min, b.min), glm::max(a.max, b.max)};
    }

    AABB Moved(const AABB& box, const Mat4& matrix)
    {
        AABB moved;
        bool first = true;
        for (const Vec3& corner : Corners(box))
        {
            const Vec3 point(matrix * Vec4(corner, 1.0f));
            moved.min = first ? point : glm::min(moved.min, point);
            moved.max = first ? point : glm::max(moved.max, point);
            first = false;
        }
        return moved;
    }

    Frustum Frustum::FromViewProjection(const Mat4& viewProjection)
    {
        // Gribb and Hartmann: each clip-space bound as a combination of the matrix's rows. glm stores columns, so row
        // r of the matrix is (m[0][r], m[1][r], m[2][r], m[3][r]).
        const auto row = [&](int r) {
            return Vec4(viewProjection[0][r], viewProjection[1][r], viewProjection[2][r], viewProjection[3][r]);
        };
        const Vec4 x = row(0);
        const Vec4 y = row(1);
        const Vec4 z = row(2);
        const Vec4 w = row(3);
        // Inside is -w <= x <= w, -w <= y <= w and, under reversed-Z with depth in [0, 1], 0 <= z <= w: the near plane
        // is where depth reaches 1, the far one where it reaches 0.
        const std::array<Vec4, 6> bounds{w + x, w - x, w + y, w - y, w - z, z};
        Frustum frustum;
        for (const Vec4& plane : bounds)
        {
            const float length = glm::length(Vec3(plane));
            // A perspective projection with an infinite far plane has a constant clip z, so its far bound has no
            // normal: there is no plane to test against.
            if (length <= 0.0f)
            {
                continue;
            }
            frustum.m_planes[frustum.m_count++] = plane / length;
        }
        return frustum;
    }

    bool Frustum::Intersects(const Sphere& sphere) const noexcept
    {
        for (const Vec4& plane : Planes())
        {
            if (glm::dot(Vec3(plane), sphere.centre) + plane.w < -sphere.radius)
            {
                return false;
            }
        }
        return true;
    }

    bool Frustum::Intersects(const AABB& box) const noexcept
    {
        return Intersects(box, Mat4(1.0f));
    }

    bool Frustum::Intersects(const AABB& box, const Mat4& transform) const noexcept
    {
        std::array<Vec4, 8> corners;
        const std::array<Vec3, 8> local = Corners(box);
        for (std::size_t corner = 0; corner < corners.size(); ++corner)
        {
            corners[corner] = Vec4(Vec3(transform * Vec4(local[corner], 1.0f)), 1.0f);
        }
        for (const Vec4& plane : Planes())
        {
            const bool allOutside = std::all_of(corners.begin(), corners.end(),
                                                [&](const Vec4& corner) { return glm::dot(plane, corner) < 0.0f; });
            if (allOutside)
            {
                return false;
            }
        }
        return true;
    }

    Sphere Frustum::SphereOf(const AABB& box, const Mat4& transform)
    {
        const Vec3 centre = (box.min + box.max) * 0.5f;
        const float halfDiagonal = glm::length(box.max - box.min) * 0.5f;
        const float scale = std::max(
            {glm::length(Vec3(transform[0])), glm::length(Vec3(transform[1])), glm::length(Vec3(transform[2]))});
        return {Vec3(transform * Vec4(centre, 1.0f)), halfDiagonal * scale};
    }
} // namespace Fenrir::Math
