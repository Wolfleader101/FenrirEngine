#pragma once

#include "FenrirMath/Math.hpp"

namespace Fenrir::Render
{
    // The CPU mirror of Fenrir/Normals.glsl (ADR-0049): unit normals as two signed components by the octahedral mapping
    // (Cigolle et al. 2014), as the depth prepass writes view-space normals into its RG16F target, so the tests can
    // read that target back and its later readers can be checked against the same maths.

    /** @brief The unit vector `n` as a point of [-1, 1]^2: FenrirOctEncode. */
    [[nodiscard]] Math::Vec2 OctahedralEncode(Math::Vec3 n) noexcept;

    /** @brief The unit vector OctahedralEncode made `e` from: FenrirOctDecode. */
    [[nodiscard]] Math::Vec3 OctahedralDecode(Math::Vec2 e) noexcept;
} // namespace Fenrir::Render
