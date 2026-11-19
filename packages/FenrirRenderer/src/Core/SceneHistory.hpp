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

    /** @brief One texel of a Hi-Z pyramid (ADR-0056, ADR-0057): the nearest and the farthest reversed-Z depth it
     * covers. */
    struct HiZTexel
    {
        /** @brief The largest depth of the texel's footprint: 0 where nothing was drawn. */
        float nearest = 0.0f;
        /** @brief The smallest depth of the texel's footprint: 0 where any of it holds nothing. */
        float farthest = 0.0f;

        friend bool operator==(const HiZTexel&, const HiZTexel&) = default;
    };

    /**
     * @brief The CPU mirror of a HiZ pass (ADR-0056): the next level of a Hi-Z pyramid from `level`, `size` texels,
     * top row first, at MipSize(size, 1). Each texel takes the largest nearest depth and the smallest farthest depth
     * of the 2 x 2 texels it covers, and of the third column or row where `size` is odd on that side and it is the
     * last.
     */
    [[nodiscard]] std::vector<HiZTexel> ReduceHiZ(std::span<const HiZTexel> level, Graphics::PixelSize size);

    /**
     * @brief One view's Hi-Z pyramid and, with reflections, its history (ADR-0056, ADR-0057), kept across frames for
     * its camera and imported into the frame's graph. Empty when the view has neither.
     */
    struct HistoryView
    {
        /**
         * @brief Its history colour, RGBA16F at the view's size with a full mip chain: last frame's until
         * SceneHistory. Empty without reflections.
         */
        GraphResource history;
        /**
         * @brief This frame's Hi-Z pyramid, RG32F (nearest and farthest depth) at the view's size with a full mip
         * chain, which HiZ draws: once after the prepass's first phase for the cull pass's second, and again from the
         * complete depth for the reflection trace.
         */
        GraphResource pyramid;
        /** @brief The pyramid's texture, for a pass that binds it outside the graph's fullscreen passes. */
        Graphics::TextureHandle pyramidTexture;
        /**
         * @brief Last frame's pyramid, the other of the two, which this frame does not draw. Empty without reflections.
         */
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

        /** @brief Whether the view has no pyramid this frame, so it adds no pass. */
        [[nodiscard]] bool IsEmpty() const noexcept
        {
            return pyramid.IsEmpty();
        }

        /** @brief Whether the view has reflections: a history and a previous pyramid beside the pyramid. */
        [[nodiscard]] bool HasReflections() const noexcept
        {
            return !history.IsEmpty();
        }
    };

    /**
     * @brief What occlusion culling and screen-space reflections read across frames (ADR-0056, ADR-0057), kept per
     * camera: a Hi-Z pyramid for a camera that culls by occlusion or reflects, and, for one that reflects, a history
     * colour and a second pyramid that swaps with the first each frame; made when a camera's view first wants them,
     * remade (and marked invalid) when its size, the samples or whether it reflects change, and freed after a frame
     * its camera draws no view with them. Its render graph passes, for each such view:
     *
     * - **HiZ**, in `DepthPrepass`: SceneDepth (the nearest and the farthest sample under MSAA) copied into the
     *   pyramid's level 0, then each level drawn from the one above, the largest nearest and the smallest farthest
     *   depth of the texels it covers (ReduceHiZ). Built after the prepass's first phase for the cull pass's second,
     *   and again after GTAO's passes, from the complete depth, for the reflection trace.
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

        /**
         * @brief Readies this frame's blocks for the views of `views` with reflections and, when `occlusion`, for every
         * view's pyramid built twice. Call once a frame, first.
         */
        void BeginFrame(std::span<const ViewRequest> views, bool occlusion);

        /**
         * @brief `view`'s textures, `size` pixels, imported into `graph`: made or remade as needed and the pyramids
         * swapped, its view-projection this frame `viewProjection` kept for the next. A view wants a pyramid when
         * `occlusion` or it reflects, and a history and a second pyramid when it reflects. Empty when it wants neither,
         * for an empty size, when its camera already had a view this frame, or when the pipelines could not be made.
         */
        [[nodiscard]] HistoryView Import(RenderGraph& graph, const ViewRequest& view, Graphics::PixelSize size,
                                         const Math::Mat4& viewProjection, bool occlusion);

        /**
         * @brief Adds `view`'s HiZ passes over `sceneDepth`, in frame `frameIndex`, one chain drawing every level of
         * its pyramid; none for an empty view. Called twice a frame for a view that culls by occlusion and reflects.
         */
        void AddHiZPasses(RenderGraph& graph, std::uint64_t frameIndex, const HistoryView& view,
                          GraphResource sceneDepth);

        /**
         * @brief Adds `view`'s SceneHistory passes over `sceneColour`, in frame `frameIndex`; none for a view without
         * reflections.
         */
        void AddHistoryPasses(RenderGraph& graph, std::uint64_t frameIndex, const HistoryView& view,
                              GraphResource sceneColour);

        /** @brief Frees the textures of every camera that had no view wanting them since BeginFrame. */
        void EndFrame();

      private:
        friend struct Fenrir::RendererProbe;

        /** @brief One camera's textures across frames. */
        struct Entry
        {
            /** @brief Empty for a camera without reflections, as is the second pyramid. */
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
