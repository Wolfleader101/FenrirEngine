#include "Core/PointShadows.hpp"

#include <algorithm>

#include "Core/Cascades.hpp"
#include "Core/SceneFrame.hpp"

namespace Fenrir::Render
{
    ShadowChoice ChoosePointShadows(std::span<const PunctualLight> lights, std::span<const Math::Vec3> cameras,
                                    std::uint32_t budget)
    {
        return ChooseShadowedLights(lights, cameras, budget, LightKind::Point);
    }

    Math::Mat3 CubeFaceTurn(std::uint32_t face) noexcept
    {
        // Each face's +x and +y directions and its axis, as FenrirFaceDirection lays them out; the view's rows are
        // those, the axis negated, since the view looks down -Z.
        struct Face
        {
            Math::Vec3 x;
            Math::Vec3 y;
            Math::Vec3 axis;
        };
        static const std::array<Face, CubeFaceCount> Faces{{
            {{0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
            {{0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}},
            {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},
            {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}},
            {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
            {{-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
        }};
        const Face& at = Faces.at(std::min(face, CubeFaceCount - 1));
        return glm::transpose(Math::Mat3(at.x, at.y, -at.axis));
    }

    PointShadow FitPointShadow(const PunctualLight& light, std::uint32_t mapSize)
    {
        PointShadow shadow;
        const auto size = static_cast<float>(std::max(mapSize, 1u));
        // A face's half width is one unit along its axis; its faces filter into each other, so none needs a margin.
        shadow.texelScale = 2.0f / size;
        shadow.near = std::clamp(light.range / 200.0f, 1e-3f, 0.1f);
        shadow.view[3] = Math::Vec4(-light.position, 1.0f);
        for (std::uint32_t face = 0; face < CubeFaceCount; ++face)
        {
            shadow.faceViews.at(face) = Math::Mat4(CubeFaceTurn(face)) * shadow.view;
        }
        shadow.projection = ReversedZPerspective(Math::DegToRad(90.0f), 1.0f, shadow.near);
        return shadow;
    }

    GpuPointShadow ToGpuPointShadow(const PunctualLight& light, const PointShadow& shadow)
    {
        GpuPointShadow gpu;
        gpu.bias = Math::Vec4(light.shadowNormalBias, light.shadowSlopeBias, shadow.texelScale, ShadowFilterRadius);
        return gpu;
    }
} // namespace Fenrir::Render
