#include "Core/ReflectionTrace.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <string_view>

#include "Core/BuiltinShaders.hpp"
#include "Core/ImageUpload.hpp"
#include "FenrirLogger/ILogger.hpp"

namespace Fenrir::Render
{
    namespace
    {
        using namespace Fenrir::Graphics;

        /** @brief The pass's block, as `builtin:reflection-trace` declares it (std140). */
        struct TraceBlock
        {
            Math::Mat4 projection{1.0f};
            Math::Mat4 inverseProjection{1.0f};
            /** @brief Last frame's view-projection times this frame's inverse view. */
            Math::Mat4 viewToPreviousClip{1.0f};
            /** @brief The view's size in pixels, and one over it. */
            Math::Vec4 size{1.0f};
            /** @brief x: the max distance; y: the thickness; z: 1 when the history holds last frame. */
            Math::Vec4 settings{0.0f};
        };
        static_assert(sizeof(TraceBlock) == 224);

        /** @brief How far past a cell's edge, in pixels, a march steps to be in the next cell. */
        constexpr float CrossOffset = 0.01f;

        /** @brief The view depth (distance in front of the camera) of reversed-Z `depth`. */
        float ViewDepth(const Math::Mat4& inverseProjection, float depth)
        {
            const Math::Vec4 position = inverseProjection * Math::Vec4(0.0f, 0.0f, std::max(depth, 1e-7f), 1.0f);
            return -position.z / position.w;
        }

        /** @brief Whether `projection` is orthographic: its w does not depend on depth. */
        bool IsOrthographic(const Math::Mat4& projection)
        {
            return projection[3][3] > 0.5f;
        }
    } // namespace

    ScreenPoint ProjectToScreen(const Math::Mat4& projection, Math::Vec3 position, PixelSize size) noexcept
    {
        const Math::Vec4 clip = projection * Math::Vec4(position, 1.0f);
        const Math::Vec3 ndc = Math::Vec3(clip) / clip.w;
        return {{(ndc.x * 0.5f + 0.5f) * static_cast<float>(size.width),
                 (0.5f - ndc.y * 0.5f) * static_cast<float>(size.height)},
                ndc.z};
    }

    Math::Vec3 ViewPositionAt(const Math::Mat4& inverseProjection, ScreenPoint point, PixelSize size) noexcept
    {
        const Math::Vec2 ndc(point.pixel.x / static_cast<float>(size.width) * 2.0f - 1.0f,
                             1.0f - point.pixel.y / static_cast<float>(size.height) * 2.0f);
        const Math::Vec4 position = inverseProjection * Math::Vec4(ndc, std::max(point.depth, 1e-7f), 1.0f);
        return Math::Vec3(position) / position.w;
    }

    ScreenRay ReflectedRay(const Math::Mat4& projection, Math::Vec3 position, Math::Vec3 normal, PixelSize size,
                           float maxDistance) noexcept
    {
        const Math::Vec3 incident =
            IsOrthographic(projection) ? Math::Vec3(0.0f, 0.0f, -1.0f) : glm::normalize(position);
        ScreenRay ray;
        ray.direction = glm::normalize(glm::reflect(incident, normal));
        ray.start = ProjectToScreen(projection, position, size);
        ray.end = ray.start;
        // A ray turning towards the camera may cross its near plane, and its fade gives it nothing anyway.
        if (ray.direction.z >= 0.0f)
        {
            return ray;
        }
        const ScreenPoint end = ProjectToScreen(projection, position + ray.direction * maxDistance, size);
        const Math::Vec2 delta = end.pixel - ray.start.pixel;
        // Cut short where it leaves the screen: depth is linear on screen, so it is cut at the same share.
        float share = 1.0f;
        const auto clip = [&](float from, float along, float edge) {
            if ((along > 0.0f && from + along > edge) || (along < 0.0f && from + along < edge))
            {
                share = std::min(share, (edge - from) / along);
            }
        };
        clip(ray.start.pixel.x, delta.x, delta.x > 0.0f ? static_cast<float>(size.width) : 0.0f);
        clip(ray.start.pixel.y, delta.y, delta.y > 0.0f ? static_cast<float>(size.height) : 0.0f);
        ray.end.pixel = ray.start.pixel + delta * share;
        ray.end.depth = ray.start.depth + (end.depth - ray.start.depth) * share;
        return ray;
    }

    HiZPyramid HiZPyramid::Of(std::span<const float> depth, PixelSize size)
    {
        HiZPyramid pyramid;
        pyramid.size = size;
        pyramid.levels.emplace_back(depth.begin(), depth.end());
        const std::uint32_t mips = MipCount(size.width, size.height);
        for (std::uint32_t level = 1; level < mips; ++level)
        {
            pyramid.levels.push_back(ReduceHiZ(pyramid.levels.back(), MipSize(size, level - 1)));
        }
        return pyramid;
    }

    float HiZPyramid::At(std::uint32_t level, std::uint32_t x, std::uint32_t y) const
    {
        return levels.at(level).at(std::size_t{y} * MipSize(size, level).width + x);
    }

    HiZHit MarchHiZ(const HiZPyramid& pyramid, const ScreenRay& ray, const Math::Mat4& inverseProjection,
                    float thickness, std::uint32_t maxIterations)
    {
        const Math::Vec3 origin(ray.start.pixel, ray.start.depth);
        const Math::Vec3 along = Math::Vec3(ray.end.pixel, ray.end.depth) - origin;
        const Math::Vec2 full(static_cast<float>(pyramid.size.width), static_cast<float>(pyramid.size.height));
        const auto top = static_cast<std::int32_t>(pyramid.levels.size()) - 1;
        constexpr float never = std::numeric_limits<float>::max();
        const auto at = [&](float t) { return origin + along * t; };

        // The texel of `level` a point is over: the last on a side takes the rest of the view, as builtin:hi-z folds.
        const auto cellOf = [&](Math::Vec2 pixel, std::int32_t level) {
            const PixelSize size = MipSize(pyramid.size, static_cast<std::uint32_t>(level));
            const float side = std::exp2(static_cast<float>(level));
            return Math::Vec2(std::clamp(std::floor(pixel.x / side), 0.0f, static_cast<float>(size.width - 1)),
                              std::clamp(std::floor(pixel.y / side), 0.0f, static_cast<float>(size.height - 1)));
        };
        // Where the ray leaves that texel, a little past its edge.
        const auto exitOf = [&](Math::Vec2 cell, std::int32_t level) {
            const PixelSize size = MipSize(pyramid.size, static_cast<std::uint32_t>(level));
            const float side = std::exp2(static_cast<float>(level));
            const auto axis = [&](float c, float last, float extent, float o, float d) {
                if (d > 0.0f)
                {
                    const float edge = c >= last ? extent : (c + 1.0f) * side;
                    return (edge + CrossOffset - o) / d;
                }
                if (d < 0.0f)
                {
                    return (c * side - CrossOffset - o) / d;
                }
                return never;
            };
            return std::min(axis(cell.x, static_cast<float>(size.width - 1), full.x, origin.x, along.x),
                            axis(cell.y, static_cast<float>(size.height - 1), full.y, origin.y, along.y));
        };

        HiZHit result;
        // Out of the starting pixel first: its depth is the surface the ray leaves.
        float t = exitOf(cellOf(Math::Vec2(origin), 0), 0);
        std::int32_t level = 0;
        while (result.iterations < maxIterations && t < 1.0f)
        {
            ++result.iterations;
            const Math::Vec3 point = at(t);
            const Math::Vec2 cell = cellOf(Math::Vec2(point), level);
            const float nearest = pyramid.At(static_cast<std::uint32_t>(level), static_cast<std::uint32_t>(cell.x),
                                             static_cast<std::uint32_t>(cell.y));
            const float exit = exitOf(cell, level);
            if (point.z > nearest)
            {
                // In front of everything in the texel: on to where it reaches the texel's nearest depth, or out.
                const float plane = along.z < 0.0f ? (nearest - origin.z) / along.z : never;
                if (plane < exit)
                {
                    if (level == 0)
                    {
                        result.hit = plane <= 1.0f;
                        t = plane;
                        break;
                    }
                    t = plane;
                    --level;
                }
                else
                {
                    t = exit;
                    level = std::min(level + 1, top);
                }
            }
            else if (level > 0)
            {
                --level;
            }
            else if (ViewDepth(inverseProjection, point.z) - ViewDepth(inverseProjection, nearest) <= thickness)
            {
                result.hit = true;
                break;
            }
            else
            {
                // Passed far behind it: past it at the finest level, where what is behind may be hit.
                t = exit;
            }
        }
        result.t = std::min(t, 1.0f);
        const Math::Vec3 stopped = at(result.t);
        result.point = {{stopped.x, stopped.y}, stopped.z};
        return result;
    }

    float ReflectionEdgeFadeAt(Math::Vec2 uv) noexcept
    {
        const float inside = std::min(std::min(uv.x, 1.0f - uv.x), std::min(uv.y, 1.0f - uv.y));
        return std::clamp(inside / ReflectionEdgeFade, 0.0f, 1.0f);
    }

    float ReflectionDistanceFadeAt(float length, float maxDistance) noexcept
    {
        return std::clamp((maxDistance - length) / (ReflectionDistanceFade * maxDistance), 0.0f, 1.0f);
    }

    float ReflectionTowardsCameraFadeAt(Math::Vec3 direction) noexcept
    {
        return std::clamp(-direction.z / ReflectionTowardsCameraFade, 0.0f, 1.0f);
    }

    PreviousPoint ReprojectToPrevious(const Math::Mat4& viewToPreviousClip, Math::Vec3 position) noexcept
    {
        const Math::Vec4 clip = viewToPreviousClip * Math::Vec4(position, 1.0f);
        const Math::Vec3 ndc = Math::Vec3(clip) / clip.w;
        PreviousPoint point{{ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f}, ndc.z};
        // Behind last frame's camera, w is negative and the divide turns the point round onto the screen.
        point.onScreen =
            clip.w > 0.0f && point.uv.x >= 0.0f && point.uv.x <= 1.0f && point.uv.y >= 0.0f && point.uv.y <= 1.0f;
        return point;
    }

    float ReflectionDisocclusion(const Math::Mat4& inverseProjection, float depth, float previousDepth,
                                 float thickness) noexcept
    {
        const float hidden = ViewDepth(inverseProjection, depth) - ViewDepth(inverseProjection, previousDepth);
        return std::clamp(1.0f - hidden / thickness, 0.0f, 1.0f);
    }

    float ReflectionRoughnessFadeAt(float roughness, float cutoff) noexcept
    {
        return std::clamp((cutoff - roughness) / ReflectionRoughnessFade, 0.0f, 1.0f);
    }

    float ReflectionMip(float roughness, float length, float viewDepth, float projectionY, float height,
                        bool orthographic, float topMip) noexcept
    {
        // Uludag's cone: GGX's a as a Phong power, and the angle holding most of that lobe.
        const float a = roughness * roughness;
        const float power = 2.0f / std::max(a * a, 1e-6f) - 2.0f;
        const float cone = std::acos(std::pow(0.244f, 1.0f / (power + 1.0f)));
        const float footprint = 2.0f * length * std::tan(cone);
        const float seenFrom = orthographic ? 1.0f : std::max(viewDepth + length, 1e-4f);
        const float pixels = footprint * projectionY * height * 0.5f / seenFrom;
        return std::clamp(std::log2(std::max(pixels, 1.0f)), 0.0f, topMip);
    }

    ReflectionTrace::ReflectionTrace(GraphicsDevice& device, ILogger& logger, std::uint32_t samples)
        : m_device(device), m_passes(device, logger, "ReflectionTrace", RenderStage::DepthPrepass,
                                     NearestClampedSampler(), sizeof(TraceBlock))
    {
        m_program = ReadBuiltin(BuiltinProgram::ReflectionTrace, logger);
        // Under MSAA the normals are multisampled, and sample 0 is read; the pyramids never are.
        constexpr std::array<std::string_view, 1> msaa{"MSAA"};
        m_pipeline = m_passes.MakePipeline(samples > 1 ? m_program.FindVariant(msaa) : m_program.FindVariant({}),
                                           m_program.name, {TextureFormat::Rgba16Float});
    }

    ReflectionTrace::~ReflectionTrace()
    {
        m_device.Destroy(m_pipeline);
    }

    void ReflectionTrace::BeginFrame(std::size_t views)
    {
        m_passes.Reserve(views);
        m_drawn.clear();
    }

    GraphResource ReflectionTrace::AddPass(RenderGraph& graph, std::uint64_t frameIndex,
                                           const ReflectionTraceView& view)
    {
        if (view.history.IsEmpty() || m_pipeline.IsEmpty())
        {
            return {};
        }
        const PixelSize size = view.history.size;
        TraceBlock block;
        block.projection = view.projection;
        block.inverseProjection = glm::inverse(view.projection);
        block.viewToPreviousClip = view.history.previousViewProjection * glm::inverse(view.view);
        const auto width = static_cast<float>(size.width);
        const auto height = static_cast<float>(size.height);
        block.size = Math::Vec4(width, height, 1.0f / width, 1.0f / height);
        block.settings =
            Math::Vec4(view.settings.maxDistance, view.settings.thickness, view.history.valid ? 1.0f : 0.0f, 0.0f);
        const BindingGroupHandle group = m_passes.Block(frameIndex, std::as_bytes(std::span(&block, 1)));

        TextureDesc desc;
        desc.format = TextureFormat::Rgba16Float;
        desc.width = size.width;
        desc.height = size.height;
        desc.usage = TextureUsage::ColourAttachment | TextureUsage::Sampled;
        const GraphResource hits = graph.CreateTexture("ReflectionHits", desc);
        const std::array<GraphResource, 3> sources{view.history.pyramid, view.sceneNormals,
                                                   view.history.previousPyramid};
        const PixelRect viewport{0, 0, size.width, size.height};
        graph.AddPass(
            "ReflectionTrace", RenderStage::DepthPrepass, QueueKind::Graphics,
            [=](PassBuilder& pass) {
                for (const GraphResource source : sources)
                {
                    pass.Sample(source);
                }
                // The pass covers its target whole, so nothing is loaded.
                pass.ColourAttachment(hits, LoadOp::DontCare);
            },
            [this, group, sources, viewport, hits](PassContext& context) {
                m_passes.Draw(context, m_pipeline, group, sources, viewport);
                m_drawn.push_back(context.Texture(hits));
            });
        return hits;
    }
} // namespace Fenrir::Render
