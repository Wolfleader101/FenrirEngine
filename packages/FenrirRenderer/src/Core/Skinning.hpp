#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "FenrirAssets/MeshData.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir::Render
{
    /**
     * @brief One palette entry as `Fenrir/Skinning.glsl` declares it, `struct FenrirBone { vec4 r0, r1, r2; }` in
     * std430 (ADR-0035): rows 0 to 2 of a column-vector affine skinning matrix `M_skin = M_model x B`, whose last row
     * is (0, 0, 0, 1). The shader applies it as `vec3(dot(r0, p), dot(r1, p), dot(r2, p))` with `p.w` 1 for a point and
     * 0 for a direction.
     */
    struct GpuBone
    {
        Math::Vec4 r0{1.0f, 0.0f, 0.0f, 0.0f};
        Math::Vec4 r1{0.0f, 1.0f, 0.0f, 0.0f};
        Math::Vec4 r2{0.0f, 0.0f, 1.0f, 0.0f};

        /** @brief Rows 0 to 2 of glm's column-major `skin`: `r_k = (m[0][k], m[1][k], m[2][k], m[3][k])`. */
        [[nodiscard]] static GpuBone FromMatrix(const Math::Mat4& skin) noexcept
        {
            GpuBone bone;
            bone.r0 = {skin[0][0], skin[1][0], skin[2][0], skin[3][0]};
            bone.r1 = {skin[0][1], skin[1][1], skin[2][1], skin[3][1]};
            bone.r2 = {skin[0][2], skin[1][2], skin[2][2], skin[3][2]};
            return bone;
        }

        /** @brief The affine matrix the rows hold, column-major, with the last row (0, 0, 0, 1). */
        [[nodiscard]] Math::Mat4 ToMatrix() const noexcept
        {
            Math::Mat4 skin(1.0f);
            for (int column = 0; column < 4; ++column)
            {
                skin[column] = Math::Vec4(r0[column], r1[column], r2[column], column == 3 ? 1.0f : 0.0f);
            }
            return skin;
        }
    };
    static_assert(sizeof(GpuBone) == 48, "FenrirBone is three vec4s, 48 bytes in std430");
    static_assert(offsetof(GpuBone, r0) == 0 && offsetof(GpuBone, r1) == 16 && offsetof(GpuBone, r2) == 32,
                  "FenrirBone's rows sit at 0, 16 and 32 bytes");

    /** @name The CPU mirror of Fenrir/Skinning.glsl
     * The shader's skinning functions, written again in C++ step for step, so tests can hold the GPU to them and
     * check their maths (ADR-0035). `skin` is the blended matrix M as rows; `world` the item's world matrix W, its
     * skeleton entity's; `normalMatrix` W's inverse transpose and `worldSign` the sign of W's determinant, as the
     * item's draw record holds them.
     * @{
     */
    /**
     * @brief The four palette entries `joints` names from `offset`, mixed by `weights` (unorm16), as
     * `FenrirSkinRows`: all-zero weights give the identity.
     */
    [[nodiscard]] GpuBone BlendBones(std::span<const GpuBone> palette, std::uint32_t offset, const BoneQuad& joints,
                                     const BoneQuad& weights);
    /** @brief A, the upper 3x3 of `skin`. */
    [[nodiscard]] Math::Mat3 SkinLinear(const GpuBone& skin);
    /**
     * @brief The bind-space normal `n` through `a`: `normalize(sign(det A) x cof(A) x n)`, the inverse transpose's
     * direction. When `a` is near-singular (|det A| under 1e-6 c^3, c its longest column) or that is degenerate or not
     * finite, `normalize(A x n)`; when that is too, `n`.
     */
    [[nodiscard]] Math::Vec3 SkinModelNormal(const Math::Mat3& a, Math::Vec3 n);
    /** @brief The vertex's world position: W x M x (p, 1). */
    [[nodiscard]] Math::Vec3 SkinPosition(const GpuBone& skin, const Math::Mat4& world, Math::Vec3 p);
    /** @brief The vertex's unit world normal: SkinModelNormal through the world normal matrix. */
    [[nodiscard]] Math::Vec3 SkinNormal(const GpuBone& skin, const Math::Mat4& normalMatrix, Math::Vec3 n);
    /**
     * @brief The vertex's unit world tangent, perpendicular to `worldNormal`, with its handedness in w: A x t through
     * the world 3x3, Gram-Schmidt'd against the normal, else the world axis least aligned with the normal made
     * perpendicular to it. Handedness is t.w x sign(det A) x sign(det W).
     */
    [[nodiscard]] Math::Vec4 SkinTangent(const GpuBone& skin, const Math::Mat4& world, float worldSign,
                                         Math::Vec4 tangent, Math::Vec3 worldNormal);
    /** @brief A rigid draw's unit world normal, as the built-in vertex stage gives it before the fragment stage. */
    [[nodiscard]] Math::Vec3 RigidNormal(const Math::Mat4& normalMatrix, Math::Vec3 n);
    /** @} */
} // namespace Fenrir::Render
