#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "Core/FullscreenPasses.hpp"
#include "Core/RenderGraph.hpp"
#include "Core/SceneFrame.hpp"
#include "Device/GraphicsDevice.hpp"
#include "FenrirAssets/ShaderContainer.hpp"

namespace Fenrir
{
    class ILogger;
} // namespace Fenrir

namespace Fenrir::Render
{
    /** @brief The most levels a view's bloom chain has, the first at half the view's size (as CoD: AW's six). */
    inline constexpr std::uint32_t MaxBloomLevels = 6;

    /**
     * @brief How many levels the bloom chain of a view of `size` has: one per halving of its shorter side, at most
     * MaxBloomLevels. 0 for a view under 2 pixels on a side, which blooms nothing.
     */
    [[nodiscard]] std::uint32_t BloomLevels(Graphics::PixelSize size) noexcept;

    /**
     * @brief How much each level of a chain of `levels` adds to the bloom, the first (half size) first, for a `radius`
     * in [0, 1]: (1 - radius) radius^i, and the last level the rest, radius^(levels - 1). They sum to 1, so the bloom
     * keeps the scene's energy. The CPU mirror of what the upsample passes' blending works out to.
     */
    [[nodiscard]] std::vector<float> BloomLevelWeights(float radius, std::uint32_t levels);

    /**
     * @brief The most levels a view's streak chain has, the first at half the width of bloom's level 0: enough that
     * the longest reaches most of the way across a 1280-wide view.
     */
    inline constexpr std::uint32_t MaxStreakLevels = 8;

    /**
     * @brief How many levels the anamorphic streak chain of a view of `size` has: one per halving of the width of its
     * bloom level 0 (half the view's size), at most MaxStreakLevels; each keeps level 0's height. 0 for a view with no
     * bloom level 0, or whose level 0 is a pixel wide, which streaks nothing.
     */
    [[nodiscard]] std::uint32_t StreakLevels(Graphics::PixelSize size) noexcept;

    /**
     * @brief How much each level of a streak chain of `levels` adds to the streaks, the narrowest first, for a
     * `length` in [0, 1]: BloomLevelWeights of it, as the upsamples blend each level over the next by the length as
     * bloom's are by the radius. They sum to 1, so the streaks keep the scene's energy.
     */
    [[nodiscard]] std::vector<float> StreakLevelWeights(float length, std::uint32_t levels);

    /** @brief One view the bloom chain blooms this frame: its SceneColor, its size, and its camera's settings. */
    struct BloomView
    {
        /** @brief The view's SceneColor, which the first downsample reads and the composite blends over. */
        GraphResource sceneColour;
        /** @brief SceneColor's size: the viewport's, in pixels. */
        Graphics::PixelSize size;
        /** @brief Its camera's intensity and radius, clamped to [0, 1] when the passes are added. */
        BloomSettings settings;
        /** @brief Its camera's anamorphic streaks, the intensity and length clamped to [0, 1] when the passes are
         * added.
         */
        AnamorphicStreaksSettings anamorphicStreaks;
        /** @brief Its camera's lens flare, whose intensity is clamped to [0, 1] and ghosts to MaxGhosts as the passes
         * are added.
         */
        LensFlareSettings lensFlare;
        /** @brief The lens dirt image the composites read, imported into the graph; empty for none. */
        GraphResource dirt;
    };

    /** @brief Which of the bloom chain's optional parts its pipelines could make: a part without them never runs. */
    struct BloomPipelines
    {
        bool streaks = true;
        bool flare = true;
        bool lensDirt = true;
    };

    /**
     * @brief What the bloom chain runs for one view this frame, decided once from its settings, its size and the
     * chain's pipelines (PlanBloom): which parts, how many levels each, and how many blocks their passes take.
     * BloomChain::AddPasses reserves its blocks and adds its passes by it.
     */
    struct BloomPlan
    {
        /** @brief An intensity over 0, and a view at least 2 pixels on a side. */
        bool bloom = false;
        /** @brief An intensity over 0, the streaks' pipelines, and at least one streak level. */
        bool streaks = false;
        /** @brief An intensity over 0, the flare's pipeline, and at least one bloom level. */
        bool flare = false;
        /** @brief Whether the composites into SceneColor (bloom's and the flare's) read the lens dirt. */
        bool lensDirt = false;
        /**
         * @brief How many of bloom's levels the view downsamples: all of them when it blooms, else down to the
         * flare's level when it flares, else level 0 for the streaks; 0 when it runs nothing.
         */
        std::uint32_t levels = 0;
        /** @brief How many streak levels it downsamples and upsamples; 0 without streaks. */
        std::uint32_t streakLevels = 0;
        /**
         * @brief How many blocks its passes take: a downsample per level and, when it blooms, an upsample per level
         * but the last and its composite; the streaks' downsample per level, as many upsamples less one and their
         * composite; and the flare's three. The composites under lens dirt take theirs from `lensDirtSlots`.
         */
        std::size_t slots = 0;
        std::size_t lensDirtSlots = 0;

        /** @brief Whether the view runs any pass. */
        [[nodiscard]] bool Runs() const noexcept
        {
            return bloom || streaks || flare;
        }
    };

    /** @brief What the bloom chain runs for `view`, with the optional parts `pipelines` makes. */
    [[nodiscard]] BloomPlan PlanBloom(const BloomView& view, BloomPipelines pipelines) noexcept;

    /**
     * @brief Physically based bloom (ADR-0049), as CoD: Advanced Warfare's (Jimenez, SIGGRAPH 2014): render graph
     * passes of the `PostProcess` stage for each view, which mix a blur of its whole SceneColor back into it before
     * Output tonemaps. There is no threshold: everything blooms a little, and only what is bright shows.
     *
     * - **BloomDownsample**, once per level (BloomLevels): SceneColor into level 0 at half its size, then each level
     *   into the next at half again, with Jimenez's 13-tap filter (five overlapping 2 x 2 boxes, the middle one at
     *   half). The first pass weighs each box by 1 / (1 + its luma), the Karis average, so a lone very bright pixel
     *   does not flicker as it moves; under MSAA it reads every sample of SceneColor, resolving as it goes.
     * - **BloomUpsample**, from the second smallest level up to level 0: the level below drawn into each with a 3 x 3
     *   tent filter, blended over it by `radius`, so each level holds itself and a falling share of every wider one
     *   (BloomLevelWeights).
     * - **BloomComposite**: level 0 drawn over SceneColor with the tent filter, blended by `intensity`, so SceneColor
     *   becomes (1 - intensity) of itself and `intensity` of the bloom.
     *
     * Anamorphic streaks (ADR-0059) are a second chain beside it, from bloom's level 0, which the Karis average has
     * already tamed:
     *
     * - **StreakDownsample**, once per level (StreakLevels): level 0 into a level at half its width and the same
     *   height, then each into the next at half the width again, with a horizontal-only filter (the `HORIZONTAL`
     *   variant of the downsample: three bilinear taps, two texels apart, weighted 1/4, 1/2 and 1/4). They run after
     *   bloom's downsamples and before its upsamples, which blend the wider levels over level 0.
     * - **StreakUpsample**, from the second narrowest level up to the first: the level below drawn into each with a
     *   horizontal tent (1 2 1, over 4), blended over it by `length` (StreakLevelWeights).
     * - **StreakComposite**, after BloomComposite: the first streak level drawn over SceneColor with the horizontal
     *   tent, times the tint at unit luminance, blended by the streaks' `intensity`. So SceneColor becomes
     *   (1 - intensity) of itself and `intensity` of the streaks, and the streaks move light, never add it: exactly
     *   so for grey light, while a coloured source times a coloured tint gains or loses a little luminance.
     *
     * The lens flare (ADR-0059), John Chapman's pseudo lens flare, reads bloom's level 1 (FlareLevel):
     *
     * - **FlareGhosts**, after the streaks' downsamples and before bloom's upsamples, which blend the wider levels
     *   over level 1, so the flare is the same with bloom on or off: `builtin:lens-flare`, the ghosts along the line
     *   through the view's centre and the halo ring, each fringed, into a level of level 1's size, the pixel keeping
     *   the rest of its own light (so a flat view is left as it is).
     * - **FlareBlur**, after it: bloom's 13-tap downsample filter, without the Karis average, at the same size, into
     *   another.
     * - **FlareComposite**, after StreakComposite: that drawn over SceneColor with the tent filter, blended by the
     *   flare's `intensity`, as BloomComposite is.
     *
     * A view whose bloom is off but whose streaks or flare are on runs bloom's first downsample (a BloomDownsample
     * pass) into a level 0 of its own, and with the flare its second into a level 1.
     *
     * With lens dirt (an image, and a dirt intensity over 0), BloomComposite and FlareComposite are the upsample's
     * `DIRT` variant, which also reads the dirt and multiplies the pass's share at each pixel by 1 + the dirt intensity
     * times the dirt's luminance, held at or below 1 (LensDirtShare). The streaks take none of it.
     *
     * Every filter's weights sum to 1 and every source is mirrored at its edges, so the bloom holds the scene's energy,
     * less what the Karis average takes from fireflies (and a little where a level's side is odd). The levels are
     * transient RGBA16F textures. A view with an intensity of 0 or less, or under 2 pixels on a side, gets no bloom
     * pass, and one with a streak or flare intensity of 0 or less (the defaults) no streak or flare pass.
     */
    class BloomChain
    {
      public:
        /** @brief Makes the passes' pipelines for SceneColor of `samples` samples per pixel (1: no MSAA). */
        BloomChain(Graphics::GraphicsDevice& device, ILogger& logger, std::uint32_t samples);
        ~BloomChain();

        BloomChain(const BloomChain&) = delete;
        BloomChain& operator=(const BloomChain&) = delete;
        BloomChain(BloomChain&&) = delete;
        BloomChain& operator=(BloomChain&&) = delete;

        /** @brief Adds the passes of every view in `views` that blooms, streaks or flares, in frame `frameIndex`. */
        void AddPasses(RenderGraph& graph, std::uint64_t frameIndex, std::span<const BloomView> views);

      private:
        Graphics::GraphicsDevice& m_device;

        ShaderContainer m_downsample;
        ShaderContainer m_upsample;
        ShaderContainer m_flare;
        /** @brief Every pass's block, source, sampler (mirrored at the edges, bilinear) and group. */
        FullscreenPasses<1> m_passes;
        /** @brief The DIRT composites' blocks and sources: their level, then the dirt. */
        FullscreenPasses<2> m_lensDirtPasses;
        /** @brief The first downsample, which reads SceneColor (its MSAA variant when SceneColor is multisampled). */
        Graphics::PipelineHandle m_firstDownsample;
        Graphics::PipelineHandle m_downsamplePipeline;
        /** @brief Into a level, blended by the radius. */
        Graphics::PipelineHandle m_upsamplePipeline;
        /** @brief Into SceneColor, at its samples, blended by the intensity. */
        Graphics::PipelineHandle m_compositePipeline;
        /** @brief The streaks' horizontal-only downsample, upsample (blended by the length) and composite. */
        Graphics::PipelineHandle m_streakDownsamplePipeline;
        Graphics::PipelineHandle m_streakUpsamplePipeline;
        Graphics::PipelineHandle m_streakCompositePipeline;
        /**
         * @brief The flare's ghosts and halo; its blur is m_downsamplePipeline, and its composite bloom's own
         * (m_compositePipeline, or m_lensDirtCompositePipeline under lens dirt).
         */
        Graphics::PipelineHandle m_flareGhostsPipeline;
        /** @brief The composite into SceneColor under lens dirt, at its samples, blended by the share the dirt
         * raises.
         */
        Graphics::PipelineHandle m_lensDirtCompositePipeline;
    };
} // namespace Fenrir::Render
