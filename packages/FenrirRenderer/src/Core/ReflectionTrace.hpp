#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "Core/FullscreenPasses.hpp"
#include "Core/RenderGraph.hpp"
#include "Core/SceneFrame.hpp"
#include "Core/SceneHistory.hpp"
#include "Device/GraphicsDevice.hpp"
#include "FenrirAssets/ShaderContainer.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    class ILogger;
    struct RendererProbe;
} // namespace Fenrir

namespace Fenrir::Render
{
    // Screen-space reflections' trace (ADR-0056), and the CPU mirror of builtin:reflection-trace's and
    // Fenrir/Lighting.glsl's maths, kept step for step with the shaders so the CPU tests check them, as
    // Core/LightClusters mirrors its shader. Points on screen are in pixels from the view's top-left corner (+y down,
    // pixel centres at .5) with their reversed-Z depth; UVs are those pixels over the view's size.

    /** @brief The most steps a reflection's Hi-Z march takes before it gives up. */
    inline constexpr std::uint32_t ReflectionMaxIterations = 64;
    /** @brief The share of the view's width and height, at each edge, over which a hit's confidence falls to 0. */
    inline constexpr float ReflectionEdgeFade = 0.1f;
    /** @brief The share of the camera's maxDistance, at its end, over which a hit's confidence falls to 0. */
    inline constexpr float ReflectionDistanceFade = 0.2f;
    /**
     * @brief The view-space z over which a mirror ray turning towards the camera fades out: 1 at -this and below, 0 at
     * 0 and above.
     */
    inline constexpr float ReflectionTowardsCameraFade = 0.1f;
    /** @brief The perceptual roughness below the camera's roughnessCutoff at which reflections start to fade out. */
    inline constexpr float ReflectionRoughnessFade = 0.1f;

    /**
     * @brief Whether a view with `settings` gets reflections: enabled, with an intensity and a max distance above 0.
     * Only then does it keep a history and pyramids, and add their passes and the trace.
     */
    [[nodiscard]] bool HasReflections(const ReflectionSettings& settings) noexcept;

    /** @brief A point on screen: in pixels from the top-left, and its reversed-Z depth. */
    struct ScreenPoint
    {
        Math::Vec2 pixel{0.0f};
        float depth = 0.0f;
    };

    /** @brief A mirror ray on screen, from the reflecting pixel to where it ends, depth linear between them. */
    struct ScreenRay
    {
        ScreenPoint start;
        ScreenPoint end;
        /** @brief The view-space direction it was reflected along, unit length. */
        Math::Vec3 direction{0.0f, 0.0f, -1.0f};
    };

    /** @brief The view-space point `position` projected by `projection` onto a view of `size` pixels. */
    [[nodiscard]] ScreenPoint ProjectToScreen(const Math::Mat4& projection, Math::Vec3 position,
                                              Graphics::PixelSize size) noexcept;

    /**
     * @brief The view-space point at `point` on a view of `size` pixels, through the projection's inverse
     * `inverseProjection`. A depth of 0 (nothing drawn, an infinite far plane) is moved just off it.
     */
    [[nodiscard]] Math::Vec3 ViewPositionAt(const Math::Mat4& inverseProjection, ScreenPoint point,
                                            Graphics::PixelSize size) noexcept;

    /**
     * @brief The mirror ray of the camera's view of the view-space point `position` about the view-space normal
     * `normal`, `maxDistance` long, on a view of `size` pixels: from where `position` is on screen to where the ray's
     * end is, cut short where it leaves the screen first.
     */
    [[nodiscard]] ScreenRay ReflectedRay(const Math::Mat4& projection, Math::Vec3 position, Math::Vec3 normal,
                                         Graphics::PixelSize size, float maxDistance) noexcept;

    /** @brief A Hi-Z pyramid on the CPU: each level's reversed-Z depths, top row first, level 0 the view's. */
    struct HiZPyramid
    {
        /** @brief Level 0's size, the view's. */
        Graphics::PixelSize size;
        std::vector<std::vector<float>> levels;

        /** @brief The pyramid of the depths `depth`, `size` texels, each level by ReduceHiZ down to 1 x 1. */
        [[nodiscard]] static HiZPyramid Of(std::span<const float> depth, Graphics::PixelSize size);

        /** @brief Level `level`'s depth at texel `x`, `y`. */
        [[nodiscard]] float At(std::uint32_t level, std::uint32_t x, std::uint32_t y) const;
    };

    /** @brief Where a march stopped. */
    struct HiZHit
    {
        /** @brief Whether the ray passed behind a texel by less than the thickness. */
        bool hit = false;
        /** @brief Where on the ray it stopped: the hit, or where it gave up. */
        ScreenPoint point;
        /** @brief How far along the screen ray it got, 0 to 1. */
        float t = 0.0f;
        /** @brief How many steps it took. */
        std::uint32_t iterations = 0;
    };

    /**
     * @brief The Hi-Z march (Uludag, GPU Pro 5) of `ray` through `pyramid`, at most `maxIterations` steps: stepping
     * down a level where the ray reaches the nearest depth of the texel it is over, up a level where it leaves the
     * texel in front of it. A hit is a level-0 texel the ray reaches or passes behind by less than `thickness` in view
     * depth, through the projection's inverse `inverseProjection`; one it passes further behind it steps over. It
     * misses when the ray ends, or the steps run out, first.
     */
    [[nodiscard]] HiZHit MarchHiZ(const HiZPyramid& pyramid, const ScreenRay& ray, const Math::Mat4& inverseProjection,
                                  float thickness, std::uint32_t maxIterations = ReflectionMaxIterations);

    /**
     * @brief The edge fade at `uv` on the view: 1 inside, falling linearly to 0 over the last ReflectionEdgeFade of
     * the view at each edge, 0 outside.
     */
    [[nodiscard]] float ReflectionEdgeFadeAt(Math::Vec2 uv) noexcept;

    /** @brief The distance fade of a ray `length` long: 1, falling linearly to 0 over the last ReflectionDistanceFade
     * of `maxDistance`. */
    [[nodiscard]] float ReflectionDistanceFadeAt(float length, float maxDistance) noexcept;

    /** @brief How much a mirror ray along the view-space `direction` is kept: 0 once it turns towards the camera. */
    [[nodiscard]] float ReflectionTowardsCameraFadeAt(Math::Vec3 direction) noexcept;

    /** @brief Where a point was in the previous frame's view. */
    struct PreviousPoint
    {
        /** @brief Its UV on that view, +y down. */
        Math::Vec2 uv{0.0f};
        /** @brief Its reversed-Z depth there. */
        float depth = 0.0f;
    };

    /**
     * @brief The view-space point `position` in the previous frame's view, through `viewToPreviousClip` (last frame's
     * view-projection times this frame's inverse view).
     */
    [[nodiscard]] PreviousPoint ReprojectToPrevious(const Math::Mat4& viewToPreviousClip, Math::Vec3 position) noexcept;

    /**
     * @brief How much of a hit at reversed-Z depth `depth` last frame was in view then, where the previous pyramid
     * held `previousDepth`: 1 when nothing was nearer, falling to 0 as what was in front of it nears `thickness` nearer
     * in view depth, through the projection's inverse `inverseProjection`.
     */
    [[nodiscard]] float ReflectionDisocclusion(const Math::Mat4& inverseProjection, float depth, float previousDepth,
                                               float thickness) noexcept;

    /**
     * @brief The roughness fade of perceptual `roughness` under the camera's `cutoff`: 1 up to ReflectionRoughnessFade
     * below it, 0 at it and above.
     */
    [[nodiscard]] float ReflectionRoughnessFadeAt(float roughness, float cutoff) noexcept;

    /**
     * @brief The history's mip a reflection of perceptual `roughness` samples: the level whose texels span the cone of
     * GGX's lobe (Uludag's fit of its width) at the ray's `length`, seen where the mirror image is, `length` beyond the
     * surface's `viewDepth`, by a projection whose y scale is `projectionY` (projection[1][1]) on a view `height`
     * pixels tall; with `orthographic` the depth is left out. Between 0 and `topMip`.
     */
    [[nodiscard]] float ReflectionMip(float roughness, float length, float viewDepth, float projectionY, float height,
                                      bool orthographic, float topMip) noexcept;

    /** @brief One view the trace runs for: its prepass's targets, its reflection inputs, its projection and settings.
     */
    struct ReflectionTraceView
    {
        /** @brief Its SceneNormals (RG16F, octahedral, view space), which its DepthPrepass pass writes. */
        GraphResource sceneNormals;
        /** @brief Its history and pyramids, which ticket 29's passes keep and draw; the trace reads its pyramids. */
        HistoryView history;
        /** @brief View to clip, reversed-Z, as the view block holds it. */
        Math::Mat4 projection{1.0f};
        /** @brief World to view this frame. */
        Math::Mat4 view{1.0f};
        /** @brief Its camera's settings. */
        ReflectionSettings settings;
    };

    /**
     * @brief Screen-space reflections' trace (ADR-0056): a render graph pass of the `DepthPrepass` stage for each view
     * with reflections, after its HiZ passes, which makes the hit target its `Opaque` pass samples
     * (`Fenrir/View.glsl`'s fenrirReflectionHits).
     *
     * **ReflectionTrace**: `builtin:reflection-trace` over the Hi-Z pyramid, SceneNormals (sample 0 under MSAA, its
     * MSAA variant) and last frame's pyramid, into a transient RGBA16F target at the view's size. For each pixel with
     * depth, the mirror ray about its prepass normal (ReflectedRay) marched through the pyramid (MarchHiZ); the hit's
     * UV in last frame's view (ReprojectToPrevious) in red and green, its confidence in blue, and the ray's view-space
     * length in alpha. The confidence is the product of the hit, the edge, distance and towards-camera fades, facing
     * (0 where the hit's normal faces along the ray), the history's validity and its UV being on last frame's screen,
     * and the disocclusion against last frame's pyramid. Pixels with nothing drawn, or no hit, are 0.
     */
    class ReflectionTrace
    {
      public:
        /** @brief Makes the pass's pipeline for normals of `samples` samples per pixel (1: no MSAA). */
        ReflectionTrace(Graphics::GraphicsDevice& device, ILogger& logger, std::uint32_t samples);
        ~ReflectionTrace();

        ReflectionTrace(const ReflectionTrace&) = delete;
        ReflectionTrace& operator=(const ReflectionTrace&) = delete;
        ReflectionTrace(ReflectionTrace&&) = delete;
        ReflectionTrace& operator=(ReflectionTrace&&) = delete;

        /** @brief Readies this frame's blocks for `views` views. Call once a frame, before AddPass. */
        void BeginFrame(std::size_t views);

        /**
         * @brief Adds the pass of `view`, one of BeginFrame's count, in frame `frameIndex`; its hit target, or empty
         * when its history is.
         */
        [[nodiscard]] GraphResource AddPass(RenderGraph& graph, std::uint64_t frameIndex,
                                            const ReflectionTraceView& view);

      private:
        friend struct Fenrir::RendererProbe;

        Graphics::GraphicsDevice& m_device;

        ShaderContainer m_program;
        /** @brief The pass's blocks, three sources, sampler (nearest, clamped: every read is a texelFetch) and group.
         */
        FullscreenPasses<3> m_passes;
        Graphics::PipelineHandle m_pipeline;
        /** @brief The hit targets this frame's passes drew into, in the order they ran; cleared by BeginFrame. */
        std::vector<Graphics::TextureHandle> m_drawn;
    };
} // namespace Fenrir::Render
