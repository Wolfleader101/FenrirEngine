#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

#include "Core/FullscreenPasses.hpp"
#include "Core/RenderGraph.hpp"
#include "Core/SceneFrame.hpp"
#include "Device/GraphicsDevice.hpp"
#include "Device/GraphicsSurface.hpp"
#include "FenrirAssets/ShaderContainer.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    class ILogger;
    struct RendererProbe;
} // namespace Fenrir

namespace Fenrir::Render
{
    /** @brief Level `level`'s size in a mip chain from `size`: halved `level` times and rounded down, at least 1. */
    [[nodiscard]] Graphics::PixelSize MipSize(Graphics::PixelSize size, std::uint32_t level) noexcept;

    /**
     * @brief The CPU mirror of a HiZ pass (ADR-0056): the next level of a Hi-Z pyramid from `level`, `size` texels of
     * reversed-Z depth, top row first, at MipSize(size, 1). Each texel is the largest (nearest) depth of the 2 x 2
     * texels it covers, and of the third column or row where `size` is odd on that side and it is the last.
     */
    [[nodiscard]] std::vector<float> ReduceHiZ(std::span<const float> level, Graphics::PixelSize size);

    /**
     * @brief One view's reflection inputs this frame (ADR-0056), kept across frames for its camera and imported into
     * the frame's graph. Empty when the view has none.
     */
    struct HistoryView
    {
        /** @brief Its history colour, RGBA16F at the view's size with a full mip chain: last frame's until
         * SceneHistory. */
        GraphResource history;
        /** @brief This frame's Hi-Z pyramid, R32F at the view's size with a full mip chain, which HiZ draws. */
        GraphResource pyramid;
        /** @brief Last frame's pyramid, the other of the two, which this frame does not draw. */
        GraphResource previousPyramid;
        /** @brief The textures' size: the view's, in pixels. */
        Graphics::PixelSize size;
        /** @brief Every texture's mip count, a full chain down to 1 x 1 (MipCount). */
        std::uint32_t mips = 0;
        /** @brief The view-projection the view had last frame, which the history and the previous pyramid were seen by.
         */
        Math::Mat4 previousViewProjection{1.0f};
        /**
         * @brief Whether the history and the previous pyramid hold last frame's view: false on the camera's first frame
         * with reflections and on the frame after they were remade.
         */
        bool valid = false;

        /** @brief Whether the view has no reflection inputs this frame, so it adds no pass. */
        [[nodiscard]] bool IsEmpty() const noexcept
        {
            return history.IsEmpty();
        }
    };

    /**
     * @brief What screen-space reflections read across frames (ADR-0056), kept per camera: a history colour and two
     * Hi-Z pyramids that swap each frame, made when a camera's view first has reflections, remade (and marked invalid)
     * when its size or the samples change, and freed after a frame its camera draws no view with them. Its render graph
     * passes, for each such view:
     *
     * - **HiZ**, in `DepthPrepass` after GTAO's: SceneDepth (sample 0 under MSAA) copied into the pyramid's level 0,
     *   then each level drawn from the one above, the largest (nearest) depth of the texels it covers (ReduceHiZ).
     * - **SceneHistory**, at the start of `PostProcess`, before bloom: SceneColor (the plain mean of its samples under
     *   MSAA) copied into the history's level 0, then each level the 2 x 2 mean of the one above.
     *
     * Every pass is a fragment pass into one mip, reading the level above as the only level it binds.
     */
    class SceneHistory
    {
      public:
        /** @brief Makes the passes' pipelines for scene targets of `samples` samples per pixel (1: no MSAA). */
        SceneHistory(Graphics::GraphicsDevice& device, ILogger& logger, std::uint32_t samples);
        ~SceneHistory();

        SceneHistory(const SceneHistory&) = delete;
        SceneHistory& operator=(const SceneHistory&) = delete;
        SceneHistory(SceneHistory&&) = delete;
        SceneHistory& operator=(SceneHistory&&) = delete;

        /** @brief Readies this frame's blocks for the views of `views` with reflections. Call once a frame, first. */
        void BeginFrame(std::span<const ViewRequest> views);

        /**
         * @brief `view`'s textures, `size` pixels, imported into `graph`: made or remade as needed and the pyramids
         * swapped, its view-projection this frame `viewProjection` kept for the next. Empty without reflections, for an
         * empty size, when its camera already had a view this frame, or when the pipelines could not be made.
         */
        [[nodiscard]] HistoryView Import(RenderGraph& graph, const ViewRequest& view, Graphics::PixelSize size,
                                         const Math::Mat4& viewProjection);

        /** @brief Adds `view`'s HiZ passes over `sceneDepth`, in frame `frameIndex`; none for an empty view. */
        void AddHiZPasses(RenderGraph& graph, std::uint64_t frameIndex, const HistoryView& view,
                          GraphResource sceneDepth);

        /** @brief Adds `view`'s SceneHistory passes over `sceneColour`, in frame `frameIndex`; none for an empty view.
         */
        void AddHistoryPasses(RenderGraph& graph, std::uint64_t frameIndex, const HistoryView& view,
                              GraphResource sceneColour);

        /** @brief Frees the textures of every camera that had no view with reflections since BeginFrame. */
        void EndFrame();

      private:
        friend struct Fenrir::RendererProbe;

        /** @brief One camera's textures across frames. */
        struct Entry
        {
            Graphics::TextureHandle history;
            std::array<Graphics::TextureHandle, 2> pyramids;
            Graphics::TextureDesc historyDesc;
            Graphics::TextureDesc pyramidDesc;
            /** @brief The pyramid this frame draws: the other was last frame's. */
            std::uint32_t current = 0;
            /** @brief The view-projection of the camera's last view. */
            Math::Mat4 viewProjection{1.0f};
            /** @brief Whether the camera has had a view this frame. */
            bool seen = false;
        };

        void Destroy(Entry& entry);
        /** @brief Adds the passes of one chain: level 0 by `first` from `source`, then each level by `down`. */
        void AddChain(FullscreenPasses<1>& passes, RenderGraph& graph, std::uint64_t frameIndex, const char* name,
                      Graphics::PipelineHandle first, Graphics::PipelineHandle down, GraphResource source,
                      GraphResource target, const HistoryView& view);

        Graphics::GraphicsDevice& m_device;
        std::uint32_t m_samples = 1;

        ShaderContainer m_hiZProgram;
        ShaderContainer m_historyProgram;
        /** @brief The HiZ passes' blocks, source, sampler (nearest, clamped: every read is a texelFetch) and group. */
        FullscreenPasses<1> m_hiZPasses;
        /** @brief The SceneHistory passes', likewise. */
        FullscreenPasses<1> m_historyPasses;
        Graphics::PipelineHandle m_hiZCopy;
        Graphics::PipelineHandle m_hiZReduce;
        Graphics::PipelineHandle m_historyCopy;
        Graphics::PipelineHandle m_historyDownsample;

        /** @brief Each camera's textures, by its entity id. */
        std::map<std::uint32_t, Entry> m_entries;
    };
} // namespace Fenrir::Render
