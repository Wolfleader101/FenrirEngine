#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "Core/BindingGroupCache.hpp"
#include "Core/BlockRing.hpp"
#include "Core/RenderGraph.hpp"
#include "Core/SceneFrame.hpp"
#include "Device/GraphicsDevice.hpp"
#include "FenrirAssets/ShaderContainer.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    class ILogger;
} // namespace Fenrir

namespace Fenrir::Render
{
    /** @brief XeGTAO's radius multiplier: the effect radius is the camera's times this, against screen-space bias. */
    inline constexpr float GtaoRadiusMultiplier = 1.457f;
    /** @brief XeGTAO's final value power: the occlusion is raised to this times the camera's intensity. */
    inline constexpr float GtaoFinalPower = 2.2f;
    /** @brief XeGTAO's blur beta: the final denoise pass's centre weight; the pass before it takes a fifth. */
    inline constexpr float GtaoBlurBeta = 1.2f;

    /** @brief Whether a view with `settings` gets GTAO passes: enabled, with a radius and an intensity above 0. */
    [[nodiscard]] bool HasAmbientOcclusion(const AmbientOcclusionSettings& settings) noexcept;

    /** @brief One view GTAO darkens: its depth prepass's targets, their size, and its projection and settings. */
    struct GtaoView
    {
        /** @brief The view's SceneDepth, which its DepthPrepass pass writes first. */
        GraphResource sceneDepth;
        /** @brief Its SceneNormals (RG16F, octahedral), which the same pass writes. */
        GraphResource sceneNormals;
        /** @brief Their size: the viewport's, in pixels. */
        Graphics::PixelSize size;
        /** @brief View to clip, reversed-Z, as the view block holds it. */
        Math::Mat4 projection{1.0f};
        /** @brief Its camera's settings: whether it has the passes, its radius and its intensity. */
        AmbientOcclusionSettings settings;
    };

    /**
     * @brief Ground-truth ambient occlusion (ADR-0049; Jimenez et al. 2016, after Intel's MIT-licensed XeGTAO):
     * render graph passes of the `DepthPrepass` stage for each view that has it, after its DepthPrepass pass, which
     * make the occlusion texture its `Opaque` pass samples (`Fenrir/View.glsl`'s fenrirAmbientOcclusion).
     *
     * - **GtaoHorizons**: `builtin:gtao`'s horizon search over SceneDepth and SceneNormals (sample 0 under MSAA, its
     *   MSAA variant), into two transient R8 targets at the view's size: the occlusion, and XeGTAO's packed edges.
     * - **GtaoDenoise**, twice: its DENOISE variant, XeGTAO's edge-aware 3 x 3 blur of the occlusion by the edges, the
     *   first pass at a fifth of the final pass's centre weight (XeGTAO's two-pass denoise, since nothing filters it
     *   over time), the second into the view's occlusion texture, a transient R8 target: 1 unoccluded, falling to
     *   0.03 at most occluded.
     *
     * The effect radius is the view's radius times GtaoRadiusMultiplier, and the occlusion is raised to GtaoFinalPower
     * times its intensity. A view whose settings are off (HasAmbientOcclusion) gets no pass.
     */
    class Gtao
    {
      public:
        /** @brief Makes the passes' pipelines for depth and normals of `samples` samples per pixel (1: no MSAA). */
        Gtao(Graphics::GraphicsDevice& device, ILogger& logger, std::uint32_t samples);
        ~Gtao();

        Gtao(const Gtao&) = delete;
        Gtao& operator=(const Gtao&) = delete;
        Gtao(Gtao&&) = delete;
        Gtao& operator=(Gtao&&) = delete;

        /**
         * @brief Readies this frame's blocks for `views` views (two each: the final denoise pass has its own). Call
         * once a frame, before AddPasses.
         */
        void BeginFrame(std::size_t views);

        /**
         * @brief Adds the passes of the view in slot `slot` (below BeginFrame's count) of frame `frameIndex`; its
         * occlusion texture, or empty when it has none.
         */
        [[nodiscard]] GraphResource AddPasses(RenderGraph& graph, std::uint64_t frameIndex, std::size_t slot,
                                              const GtaoView& view);

      private:
        [[nodiscard]] Graphics::PipelineHandle MakePipeline(const ShaderVariant* variant, std::size_t outputs);
        /** @brief The group binding `first` and `second` as a pass's sources in frame `frameIndex`, made once while
         * both live. */
        [[nodiscard]] Graphics::BindingGroupHandle SourceGroup(Graphics::TextureHandle first,
                                                               Graphics::TextureHandle second,
                                                               std::uint64_t frameIndex);

        Graphics::GraphicsDevice& m_device;
        ILogger& m_logger;

        ShaderContainer m_program;
        /** @brief Set 1: the pass's block. */
        Graphics::BindingGroupLayoutHandle m_blockLayout;
        /** @brief Set 2: the two textures it reads. */
        Graphics::BindingGroupLayoutHandle m_sourceLayout;
        Graphics::PipelineHandle m_horizons;
        Graphics::PipelineHandle m_denoise;
        /** @brief Nearest, clamped: every read is a texelFetch. */
        Graphics::SamplerHandle m_sampler;
        std::unique_ptr<BlockRing> m_blocks;
        /** @brief Each pair of source textures' group, while both live. */
        BindingGroupCache<2> m_sourceGroups;
    };
} // namespace Fenrir::Render
