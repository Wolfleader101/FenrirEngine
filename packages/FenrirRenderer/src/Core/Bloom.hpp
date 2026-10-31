#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "Core/BlockRing.hpp"
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

    /** @brief One view the bloom chain blooms this frame: its SceneColor, its size, and its camera's settings. */
    struct BloomView
    {
        /** @brief The view's SceneColor, which the first downsample reads and the composite blends over. */
        GraphResource sceneColour;
        /** @brief SceneColor's size: the viewport's, in pixels. */
        Graphics::PixelSize size;
        /** @brief Its camera's intensity and radius, clamped to [0, 1] when the passes are added. */
        BloomSettings settings;
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
     * Every filter's weights sum to 1 and every source is mirrored at its edges, so the bloom holds the scene's energy,
     * less what the Karis average takes from fireflies (and a little where a level's side is odd). The levels are
     * transient RGBA16F textures. A view with an
     * intensity of 0 or less, or under 2 pixels on a side, gets no pass.
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

        /** @brief Adds the passes of every view in `views` that blooms, in frame `frameIndex`. */
        void AddPasses(RenderGraph& graph, std::uint64_t frameIndex, std::span<const BloomView> views);

      private:
        [[nodiscard]] Graphics::PipelineHandle MakePipeline(const ShaderContainer& container,
                                                            const ShaderVariant* variant, Graphics::BlendMode blend,
                                                            std::uint32_t samples);
        /** @brief The group binding `texture` as a pass's source, made once while the texture lives. */
        [[nodiscard]] Graphics::BindingGroupHandle SourceGroup(Graphics::TextureHandle texture);

        Graphics::GraphicsDevice& m_device;
        ILogger& m_logger;

        ShaderContainer m_downsample;
        ShaderContainer m_upsample;
        /** @brief Set 1 of both programs: the pass's block. */
        Graphics::BindingGroupLayoutHandle m_blockLayout;
        /** @brief Set 2 of both programs: the texture the pass reads. */
        Graphics::BindingGroupLayoutHandle m_sourceLayout;
        /** @brief The first downsample, which reads SceneColor (its MSAA variant when SceneColor is multisampled). */
        Graphics::PipelineHandle m_firstDownsample;
        Graphics::PipelineHandle m_downsamplePipeline;
        /** @brief Into a level, blended by the radius. */
        Graphics::PipelineHandle m_upsamplePipeline;
        /** @brief Into SceneColor, at its samples, blended by the intensity. */
        Graphics::PipelineHandle m_compositePipeline;
        /** @brief Mirrored at the edges, bilinear. */
        Graphics::SamplerHandle m_sampler;
        std::unique_ptr<BlockRing> m_blocks;
        /** @brief Each source texture's group, by its handle's index and generation, while the texture lives. */
        std::map<std::pair<std::uint32_t, std::uint32_t>, Graphics::BindingGroupHandle> m_sourceGroups;
    };
} // namespace Fenrir::Render
