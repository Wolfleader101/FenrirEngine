#include "Core/PointShadows.hpp"

#include <algorithm>
#include <span>

#include "Core/Cascades.hpp"
#include "Core/SceneFrame.hpp"

namespace Fenrir::Render
{
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

    PointShadow PointShadowKind::Fit(const PunctualLight& light, const ShadowBudgetConfig& config)
    {
        return FitPointShadow(light, config.mapSize);
    }

    GpuPointShadow PointShadowKind::ToRecord(const PunctualLight& light, const PointShadow& shadow, std::uint32_t,
                                             const ShadowBudgetConfig&)
    {
        return ToGpuPointShadow(light, shadow);
    }

    Graphics::TextureDesc PointShadowKind::MapDesc(const ShadowBudgetConfig& config)
    {
        Graphics::TextureDesc desc;
        desc.kind = Graphics::TextureKind::CubeArray;
        desc.format = Graphics::TextureFormat::Depth32Float;
        desc.width = config.mapSize;
        desc.height = config.mapSize;
        desc.layers = config.budget;
        desc.usage = Graphics::TextureUsage::DepthAttachment | Graphics::TextureUsage::Sampled;
        return desc;
    }

    Graphics::TextureHandle PointShadowKind::MakeFallback(Graphics::GraphicsDevice& device)
    {
        Graphics::TextureDesc desc;
        desc.kind = Graphics::TextureKind::CubeArray;
        desc.format = Graphics::TextureFormat::Depth32Float;
        desc.usage = Graphics::TextureUsage::Sampled | Graphics::TextureUsage::CopyDestination;
        const Graphics::TextureHandle cube = device.CreateTexture(desc, "No point shadow");
        constexpr float farthest = 0.0f;
        for (std::uint32_t face = 0; face < CubeFaceCount; ++face)
        {
            device.WriteTexture(cube, 0, face, std::as_bytes(std::span(&farthest, 1)));
        }
        return cube;
    }
} // namespace Fenrir::Render
