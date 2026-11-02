#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "Core/PunctualLights.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir::Render
{
    // Light clustering (ADR-0049): each view's frustum cut into 16 x 9 tiles across its viewport and 24 slices in
    // depth, spaced exponentially from the near plane to the farthest any light reaches, and each point, spot and rect
    // light listed in every cluster its range's sphere touches. A compute pass per view writes the lists
    // (`builtin:light-clusters`) and `Fenrir/Lighting.glsl` loops over its pixel's. These are the CPU mirror of both,
    // kept step for step with the shaders so the CPU tests check their maths. View space looks down -Z; a view depth
    // is the distance along it, -z.

    /** @name The cluster grid
     * @{
     */
    inline constexpr std::uint32_t ClusterColumns = 16;
    inline constexpr std::uint32_t ClusterRows = 9;
    inline constexpr std::uint32_t ClusterSlices = 24;
    inline constexpr std::uint32_t ClusterCount = ClusterColumns * ClusterRows * ClusterSlices;
    /** @} */

    /**
     * @brief The most lights a cluster lists. Those past it, in light buffer order, light nothing in that cluster: a
     * pixel's loop stays bounded however many lights crowd round it.
     */
    inline constexpr std::uint32_t MaxClusterLights = 256;

    /** @brief The view depths the slices run between: the first's near face and the last's far face. */
    struct ClusterDepths
    {
        float near = 0.1f;
        float far = 1.0f;
    };

    /** @brief A box in view space, `min` to `max`. */
    struct ClusterBox
    {
        Math::Vec3 min{0.0f};
        Math::Vec3 max{0.0f};
    };

    /** @brief The sphere a light reaches: its centre in view space and its radius. */
    struct LightSphere
    {
        Math::Vec3 centre{0.0f};
        float radius = 0.0f;
    };

    /** @brief A cluster by its tile's column (from the left), row (from the top) and slice (from the camera). */
    struct ClusterCell
    {
        std::uint32_t column = 0;
        std::uint32_t row = 0;
        std::uint32_t slice = 0;
    };

    /**
     * @brief The sphere `light` reaches in the view `view` takes world space to: a point or spot light's range about
     * its position, and a rect light's range past its corners about its centre, where its window measures from the
     * rect's nearest point.
     */
    [[nodiscard]] LightSphere LightSphereOf(const GpuLight& light, const Math::Mat4& view) noexcept;

    /**
     * @brief The slices' depths for the reversed-Z projection `projection` and the lights' spheres `spheres`: from its
     * near plane, held at least 1e-3, to the farthest view depth a sphere reaches, held a little past the near plane
     * and, for an orthographic projection, no farther than its far plane.
     */
    [[nodiscard]] ClusterDepths ClusterDepthsOf(const Math::Mat4& projection,
                                                std::span<const LightSphere> spheres) noexcept;

    /** @brief The view depth of slice `slice`'s near face, near x (far / near)^(slice / 24); slice 24 is `far`. */
    [[nodiscard]] float ClusterSliceDepth(std::uint32_t slice, ClusterDepths depths) noexcept;

    /** @brief 24 / ln(far / near): what the log of a view depth over `near` is scaled by to find its slice. */
    [[nodiscard]] float ClusterSliceScale(ClusterDepths depths) noexcept;

    /** @brief The slice the view depth `depth` is in, 0 nearer than `near` and the last past `far`. */
    [[nodiscard]] std::uint32_t ClusterSliceOf(float depth, ClusterDepths depths) noexcept;

    /** @brief The index of `cell` in the grid and in each view's lists: column, then row, then slice. */
    [[nodiscard]] std::uint32_t ClusterIndex(ClusterCell cell) noexcept;

    /** @brief The cell of the cluster at index `index`. */
    [[nodiscard]] ClusterCell ClusterCellOf(std::uint32_t index) noexcept;

    /**
     * @brief The view-space box round cluster `cell` of a view projected by `projection`, whose inverse is
     * `inverseProjection`: its tile's four corners in NDC, on their lines of sight, at its slice's near and far depths
     * (the first slice's from the near plane itself), the edge tiles' reaching a little past the viewport, padded by a
     * hundredth of its size, so every point found in the cluster is inside it.
     */
    [[nodiscard]] ClusterBox ClusterBoundsOf(const Math::Mat4& inverseProjection, ClusterDepths depths,
                                             ClusterCell cell) noexcept;

    /** @brief Whether `sphere` touches `box`: the box's nearest point to its centre is within its radius. */
    [[nodiscard]] bool SphereTouchesBox(const LightSphere& sphere, const ClusterBox& box) noexcept;

    /**
     * @brief The cluster the view-space point `position` is in, as Fenrir/Lighting.glsl finds a pixel's: its tile from
     * where `projection` puts it in NDC, clamped into the grid, and its slice from its depth.
     */
    [[nodiscard]] ClusterCell ClusterOf(const Math::Vec3& position, const Math::Mat4& projection,
                                        ClusterDepths depths) noexcept;

    /** @brief A view's clusters as the compute pass lists them. */
    struct ClusterAssignment
    {
        ClusterDepths depths;
        /** @brief Each cluster's lights by light buffer index, ascending, at most MaxClusterLights, by ClusterIndex. */
        std::vector<std::vector<std::uint32_t>> lights;
    };

    /** @brief Every light of `lights` listed in the clusters its sphere touches, for the view `view` and `projection`.
     */
    [[nodiscard]] ClusterAssignment AssignClusters(std::span<const GpuLight> lights, const Math::Mat4& view,
                                                   const Math::Mat4& projection);
} // namespace Fenrir::Render
