// The CPU mirror of Fenrir/Skinning.glsl (ADR-0035): kept step for step with the shader, which the GL probes hold it
// to, so a change to one is a change to both.
#include "Core/Skinning.hpp"

#include <algorithm>
#include <cmath>

namespace Fenrir::Render
{
    namespace
    {
        bool IsFinite(Math::Vec3 v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        /** @brief The unit vector along `v`, or `fallback` when `v` is too short or not finite (FenrirNormaliseOr). */
        Math::Vec3 NormaliseOr(Math::Vec3 v, Math::Vec3 fallback)
        {
            const float length2 = glm::dot(v, v);
            if (!IsFinite(v) || !std::isfinite(length2) || length2 < 1e-12f)
            {
                return fallback;
            }
            return v / std::sqrt(length2);
        }
    } // namespace

    GpuBone BlendBones(std::span<const GpuBone> palette, std::uint32_t offset, const BoneQuad& joints,
                       const BoneQuad& weights)
    {
        GpuBone blended{Math::Vec4(0.0f), Math::Vec4(0.0f), Math::Vec4(0.0f)};
        float total = 0.0f;
        for (std::size_t i = 0; i < 4; ++i)
        {
            const float weight = static_cast<float>(weights.at(i)) / static_cast<float>(BoneWeightOne);
            const std::size_t index = std::size_t{offset} + joints.at(i);
            total += weight;
            if (weight == 0.0f || index >= palette.size())
            {
                continue;
            }
            blended.r0 += palette[index].r0 * weight;
            blended.r1 += palette[index].r1 * weight;
            blended.r2 += palette[index].r2 * weight;
        }
        return total <= 0.0f ? GpuBone{} : blended;
    }

    Math::Mat3 SkinLinear(const GpuBone& skin)
    {
        return {Math::Vec3(skin.r0.x, skin.r1.x, skin.r2.x), Math::Vec3(skin.r0.y, skin.r1.y, skin.r2.y),
                Math::Vec3(skin.r0.z, skin.r1.z, skin.r2.z)};
    }

    Math::Vec3 SkinModelNormal(const Math::Mat3& a, Math::Vec3 n)
    {
        const Math::Vec3 c0 = glm::cross(a[1], a[2]);
        const Math::Vec3 c1 = glm::cross(a[2], a[0]);
        const Math::Vec3 c2 = glm::cross(a[0], a[1]);
        const float det = glm::dot(a[0], c0);
        const float c = std::max({glm::length(a[0]), glm::length(a[1]), glm::length(a[2])});
        const Math::Vec3 bind = NormaliseOr(n, Math::Vec3(0.0f, 1.0f, 0.0f));
        const Math::Vec3 direct = NormaliseOr(a * n, bind);
        // Written as a negated >=, so a NaN determinant takes the fallback too.
        if (!(std::abs(det) >= 1e-6f * c * c * c))
        {
            return direct;
        }
        const Math::Vec3 cofactor = (det < 0.0f ? -1.0f : 1.0f) * (Math::Mat3(c0, c1, c2) * n);
        return NormaliseOr(cofactor, direct);
    }

    Math::Vec3 SkinPosition(const GpuBone& skin, const Math::Mat4& world, Math::Vec3 p)
    {
        const Math::Vec4 point(p, 1.0f);
        const Math::Vec3 model(glm::dot(skin.r0, point), glm::dot(skin.r1, point), glm::dot(skin.r2, point));
        return Math::Vec3(world * Math::Vec4(model, 1.0f));
    }

    Math::Vec3 SkinNormal(const GpuBone& skin, const Math::Mat4& normalMatrix, Math::Vec3 n)
    {
        const Math::Vec3 model = SkinModelNormal(SkinLinear(skin), n);
        return NormaliseOr(Math::Mat3(normalMatrix) * model, model);
    }

    Math::Vec4 SkinTangent(const GpuBone& skin, const Math::Mat4& world, float worldSign, Math::Vec4 tangent,
                           Math::Vec3 worldNormal)
    {
        const Math::Mat3 a = SkinLinear(skin);
        Math::Vec3 t = Math::Mat3(world) * (a * Math::Vec3(tangent));
        t -= worldNormal * glm::dot(worldNormal, t);
        const Math::Vec3 n = glm::abs(worldNormal);
        const Math::Vec3 axis = n.x <= n.y && n.x <= n.z ? Math::Vec3(1.0f, 0.0f, 0.0f)
                                : n.y <= n.z             ? Math::Vec3(0.0f, 1.0f, 0.0f)
                                                         : Math::Vec3(0.0f, 0.0f, 1.0f);
        const Math::Vec3 perpendicular = glm::normalize(axis - worldNormal * glm::dot(worldNormal, axis));
        const Math::Vec3 unit = glm::length(t) < 1e-6f ? perpendicular : NormaliseOr(t, perpendicular);
        const float signA = glm::determinant(a) < 0.0f ? -1.0f : 1.0f;
        return {unit, tangent.w * signA * worldSign};
    }

    Math::Vec3 RigidNormal(const Math::Mat4& normalMatrix, Math::Vec3 n)
    {
        return NormaliseOr(Math::Mat3(normalMatrix) * n, n);
    }
} // namespace Fenrir::Render
