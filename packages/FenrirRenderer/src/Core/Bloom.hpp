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
        StreakSettings streaks;
    };

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
     * A view whose bloom is off but whose streaks are on runs bloom's first downsample (a BloomDownsample pass) into a
     * level 0 of its own.
     *
     * Every filter's weights sum to 1 and every source is mirrored at its edges, so the bloom holds the scene's energy,
     * less what the Karis average takes from fireflies (and a little where a level's side is odd). The levels are
     * transient RGBA16F textures. A view with an intensity of 0 or less, or under 2 pixels on a side, gets no bloom
     * pass, and one with a streak intensity of 0 or less (the default) no streak pass.
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

        /** @brief Adds the passes of every view in `views` that blooms or streaks, in frame `frameIndex`. */
        void AddPasses(RenderGraph& graph, std::uint64_t frameIndex, std::span<const BloomView> views);

      private:
        Graphics::GraphicsDevice& m_device;

        ShaderContainer m_downsample;
        ShaderContainer m_upsample;
        /** @brief Every pass's block, source, sampler (mirrored at the edges, bilinear) and group. */
        FullscreenPasses<1> m_passes;
        /** @brief The first downsample, which reads SceneColor (its MSAA variant when SceneColor is multisampled). */
        Graphics::PipelineHandle m_firstDownsample;
        Graphics::PipelineHandle m_downsamplePipeline;
        /** @brief Into a level, blended by the radius. */
        Graphics::PipelineHandle m_upsamplePipeline;
        /** @brief Into SceneColor, at its samples, blended by the intensity. */
        Graphics::PipelineHandle m_compositePipeline;
        /** @brief The streaks' horizontal-only downsample, upsample (blended by the length) and composite. */
        Graphics::PipelineHandle m_streakDownsample;
        Graphics::PipelineHandle m_streakUpsample;
        Graphics::PipelineHandle m_streakComposite;
    };
} // namespace Fenrir::Render
