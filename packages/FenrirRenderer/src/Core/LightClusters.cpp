#include "Core/LightClusters.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "Core/Cascades.hpp"

namespace Fenrir::Render
{
    namespace
    {
        /** @brief The nearest the first slice may start, so the slices' logarithm stays finite. */
        constexpr float MinClusterNear = 1e-3f;
        /** @brief How far past the near plane the last slice reaches at least, so the slices never collapse. */
        constexpr float MinClusterDepthRatio = 1.01f;
        /**
         * @brief How far past the viewport, in NDC, the edge tiles' boxes reach: a sample MSAA interpolates outside its
         * triangle can sit past the viewport's edge, and is clamped into an edge tile.
         */
        constexpr float ClusterEdgeReach = 0.05f;
        /** @brief The share of its size each box is padded by, so a point rounded onto a boundary is still inside. */
        constexpr float ClusterPadding = 0.01f;

        /** @brief The view-space point the inverse projection `inverse` takes the NDC point (x, y, z) back to. */
        Math::Vec3 Unproject(const Math::Mat4& inverse, float x, float y, float z) noexcept
        {
            const Math::Vec4 point = inverse * Math::Vec4(x, y, z, 1.0f);
            return Math::Vec3(point) / point.w;
        }

    } // namespace

    LightSphere LightSphereOf(const GpuLight& light, const Math::Mat4& view) noexcept
    {
        float radius = light.range;
        if (light.kind == static_cast<std::uint32_t>(LightKind::Rect))
        {
            radius +=
                std::sqrt(Math::Dot(light.halfWidth, light.halfWidth) + Math::Dot(light.halfHeight, light.halfHeight));
        }
        return {Math::Vec3(view * Math::Vec4(light.position, 1.0f)), radius};
    }

    ClusterDepths ClusterDepthsOf(const Math::Mat4& projection, std::span<const LightSphere> spheres) noexcept
    {
        // A perspective's far plane is at infinity, so it holds nothing back.
        const DepthRange planes = DepthRangeOf(projection);
        const float near = std::max(planes.near, MinClusterNear);
        float far = near;
        for (const LightSphere& sphere : spheres)
        {
            far = std::max(far, -sphere.centre.z + sphere.radius);
        }
        return {near, std::max(std::min(far, planes.far), near * MinClusterDepthRatio)};
    }

    float ClusterSliceDepth(std::uint32_t slice, ClusterDepths depths) noexcept
    {
        if (slice >= ClusterSlices)
        {
            return depths.far;
        }
        return depths.near *
               std::pow(depths.far / depths.near, static_cast<float>(slice) / static_cast<float>(ClusterSlices));
    }

    float ClusterSliceScale(ClusterDepths depths) noexcept
    {
        return static_cast<float>(ClusterSlices) / std::log(depths.far / depths.near);
    }

    std::uint32_t ClusterSliceOf(float depth, ClusterDepths depths) noexcept
    {
        if (!(depth > depths.near))
        {
            return 0;
        }
        const float slice = std::floor(std::log(depth / depths.near) * ClusterSliceScale(depths));
        return static_cast<std::uint32_t>(std::min(slice, static_cast<float>(ClusterSlices - 1)));
    }

    std::uint32_t ClusterIndex(ClusterCell cell) noexcept
    {
        return cell.column + ClusterColumns * (cell.row + ClusterRows * cell.slice);
    }

    ClusterCell ClusterCellOf(std::uint32_t index) noexcept
    {
        return {index % ClusterColumns, (index / ClusterColumns) % ClusterRows, index / (ClusterColumns * ClusterRows)};
    }

    ClusterBox ClusterBoundsOf(const Math::Mat4& inverseProjection, ClusterDepths depths, ClusterCell cell) noexcept
    {
        const auto edge = [](std::uint32_t at, std::uint32_t count) {
            const float ndc = -1.0f + 2.0f * static_cast<float>(at) / static_cast<float>(count);
            return at == 0 ? ndc - ClusterEdgeReach : at == count ? ndc + ClusterEdgeReach : ndc;
        };
        const float x0 = edge(cell.column, ClusterColumns);
        const float x1 = edge(cell.column + 1, ClusterColumns);
        const float y0 = edge(cell.row, ClusterRows);
        const float y1 = edge(cell.row + 1, ClusterRows);
        const float far = ClusterSliceDepth(cell.slice + 1, depths);
        ClusterBox box{Math::Vec3(INFINITY), Math::Vec3(-INFINITY)};
        for (const Math::Vec2 corner : {Math::Vec2(x0, y0), Math::Vec2(x1, y0), Math::Vec2(x0, y1), Math::Vec2(x1, y1)})
        {
            // The corner's line of sight, through its points on the near plane and at NDC depth 0.5 (twice the near
            // plane's depth for a perspective), so an orthographic projection's parallel lines work as well. The first
            // slice starts at the near plane itself, which an orthographic projection may put at or behind the camera.
            const Math::Vec3 a = Unproject(inverseProjection, corner.x, corner.y, 1.0f);
            const Math::Vec3 b = Unproject(inverseProjection, corner.x, corner.y, 0.5f);
            const auto at = [&](float depth) { return a + (b - a) * ((depth + a.z) / (a.z - b.z)); };
            for (const Math::Vec3 point : {cell.slice == 0 ? a : at(ClusterSliceDepth(cell.slice, depths)), at(far)})
            {
                box.min = glm::min(box.min, point);
                box.max = glm::max(box.max, point);
            }
        }
        const Math::Vec3 padding = (box.max - box.min) * ClusterPadding;
        return {box.min - padding, box.max + padding};
    }

    bool SphereTouchesBox(const LightSphere& sphere, const ClusterBox& box) noexcept
    {
        const Math::Vec3 nearest = glm::clamp(sphere.centre, box.min, box.max);
        const Math::Vec3 gap = sphere.centre - nearest;
        return Math::Dot(gap, gap) <= sphere.radius * sphere.radius;
    }

    ClusterCell ClusterOf(const Math::Vec3& position, const Math::Mat4& projection, ClusterDepths depths) noexcept
    {
        const Math::Vec4 clip = projection * Math::Vec4(position, 1.0f);
        const Math::Vec2 ndc = Math::Vec2(clip) / clip.w;
        const auto tile = [](float coordinate, std::uint32_t count) {
            const float at = std::floor((coordinate * 0.5f + 0.5f) * static_cast<float>(count));
            return static_cast<std::uint32_t>(std::clamp(at, 0.0f, static_cast<float>(count - 1)));
        };
        return {tile(ndc.x, ClusterColumns), tile(ndc.y, ClusterRows), ClusterSliceOf(-position.z, depths)};
    }

    ClusterAssignment AssignClusters(std::span<const GpuLight> lights, const Math::Mat4& view,
                                     const Math::Mat4& projection)
    {
        std::vector<LightSphere> spheres;
        spheres.reserve(lights.size());
        for (const GpuLight& light : lights)
        {
            spheres.push_back(LightSphereOf(light, view));
        }
        ClusterAssignment assignment{ClusterDepthsOf(projection, spheres), {}};
        assignment.lights.resize(ClusterCount);
        const Math::Mat4 inverse = glm::inverse(projection);
        for (std::uint32_t c = 0; c < ClusterCount; ++c)
        {
            const ClusterBox box = ClusterBoundsOf(inverse, assignment.depths, ClusterCellOf(c));
            std::vector<std::uint32_t>& listed = assignment.lights[c];
            for (std::uint32_t i = 0; i < spheres.size() && listed.size() < MaxClusterLights; ++i)
            {
                if (SphereTouchesBox(spheres[i], box))
                {
                    listed.push_back(i);
                }
            }
        }
        return assignment;
    }
} // namespace Fenrir::Render
