#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Core/BlockRing.hpp"
#include "Core/RenderFeature.hpp"
#include "Core/SceneFrame.hpp"
#include "Core/SceneRenderer.hpp"
#include "FenrirAssets/ShaderContainer.hpp"
#include "FenrirAssets/ShaderData.hpp"
#include "FenrirDraw2D/DrawList2D.hpp"

namespace Fenrir
{
    class ILogger;
    struct RendererProbe;
} // namespace Fenrir

namespace Fenrir::Render
{
    /** @brief One 2D draw list submitted to the window overlay, on its overlay layer. */
    struct Submission2D
    {
        std::int32_t layer = 0;
        DrawList2D list;
    };

    /** @brief What the window overlay draws in one frame: plain data the facade copies in, nothing kept after it. */
    struct OverlayFrame
    {
        /** @brief In submission order. */
        std::vector<Submission2D> submissions;
        /** @brief The window's pixels per logical unit. */
        float contentScale = 1.0f;
        /**
         * @brief The texture the overlay draws into instead of the window's backbuffer, with its description; empty
         * for the window. The facade always draws into the window; the core's tests name an offscreen target.
         */
        Graphics::TextureHandle target;
        Graphics::TextureDesc targetDesc;
    };

    /**
     * @brief The 2D render feature (ADR-0027): draws 2D draw lists into a 2D target. The window overlay is the only
     * target built: one `Overlay 2D` pass after every view's Output, before the debug feature's Overlay pass, so debug
     * screen text stays the fixed top layer. A frame with no submissions adds no pass.
     *
     * **The target** maps local logical coordinates (top-left origin, Y down) to clip space from the window's logical
     * size, its pixel size divided by the content scale, through set 0 of every 2D shader (`Fenrir/2D.glsl`). There is
     * no depth and no culling. A command's scissor is scaled by the content scale, rounded outwards to whole pixels and
     * clamped to the target; one that covers no pixel draws nothing.
     *
     * **Order:** overlay layers lowest first, then submission order, then each list's command order.
     *
     * **Realising.** Each frame the vertex, index and parameter bytes are copied into per-frame ring buffers. Each 2D
     * pipeline description becomes a device pipeline the first time it is drawn, cached by value with the target's
     * format while its shader lives, so equal descriptions share one. Blend modes take ADR-0027's colour and alpha
     * factors. The `Gamma` blend space turns the target's encoding off, and is allowed only on an 8-bit sRGB target;
     * elsewhere it is logged as an error and the pipeline draws as `Linear`. The variant drawn is picked from the
     * description (see PipelineDescription2D).
     *
     * **Set 1** is per command and laid out from the shader's reflection: its `MaterialParams` block, at binding 0 if
     * the shader has one, is the block's parameter defaults with the command's parameter bytes written over its start;
     * then its textures, slot by slot in binding order, each an image prepared as materials' are (SceneRenderer's) and
     * bound with its slot's sampler. A missing, stale or unprepared image binds the texture's parameter default.
     *
     * **Skipped commands,** each reason logged once: more than MaxTextureSlots2D texture slots or MaxParameterBytes2D
     * parameter bytes, a pipeline index or an index range outside its list, and a shader that is stale or not a 2D
     * shader.
     */
    class Draw2DFeature final : public RenderFeature
    {
      public:
        /** @brief Draws on `device`, sampling the images `images` prepares. */
        Draw2DFeature(Graphics::GraphicsDevice& device, ILogger& logger, SceneRenderer& images);
        ~Draw2DFeature() override;

        Draw2DFeature(const Draw2DFeature&) = delete;
        Draw2DFeature& operator=(const Draw2DFeature&) = delete;
        Draw2DFeature(Draw2DFeature&&) = delete;
        Draw2DFeature& operator=(Draw2DFeature&&) = delete;

        /** @name Prepare
         * The 2D shaders draw lists name, prepared as SceneRenderer prepares assets: IsShaderCurrent says whether `key`
         * is at `revision`, PrepareShader (re)reads it, dropping the pipelines made from it, and ReleaseShader frees
         * them. A container that does not read, or is not a 2D shader, is logged and draws nothing.
         * @{
         */
        [[nodiscard]] bool IsShaderCurrent(AssetKey key, std::uint32_t revision) const;
        void PrepareShader(AssetKey key, std::uint32_t revision, const ShaderData& shader);
        void ReleaseShader(AssetKey key);
        [[nodiscard]] std::vector<AssetKey> ShaderKeys() const;
        /** @} */

        /** @brief What the next frame draws. */
        void SetFrame(OverlayFrame frame);

        void AddPasses(RenderGraph& graph, const FrameContext& frame) override;

      private:
        friend struct Fenrir::RendererProbe;

        /** @brief A buffer written each frame, one per frame in flight, grown as needed. */
        class ByteRing;

        /** @brief A prepared 2D shader, with set 1's layout for each of its variants. */
        struct ShaderEntry
        {
            std::uint32_t revision = 0;
            /** @brief Empty when the container did not read or is not a 2D shader. */
            std::unique_ptr<ShaderContainer> container;
            std::map<const ShaderVariant*, Graphics::BindingGroupLayoutHandle> layouts;
        };

        /** @brief A realised pipeline: what it was made for, and what it draws with. */
        struct PipelineEntry
        {
            PipelineDescription2D description;
            Graphics::TextureFormat format = Graphics::TextureFormat::Rgba8Srgb;
            Graphics::PipelineHandle pipeline;
            const ShaderVariant* variant = nullptr;
            Graphics::BindingGroupLayoutHandle layout;
        };

        /** @brief One command resolved for recording. */
        struct Draw
        {
            Graphics::PipelineHandle pipeline;
            /** @brief Its set 1 group: one more than its place in the frame's pending groups, 0 for none. */
            std::size_t groupIndex = 0;
            Graphics::BindingGroupHandle group;
            std::size_t vertexOffset = 0;
            std::size_t indexOffset = 0;
            Graphics::IndexFormat indexFormat = Graphics::IndexFormat::UInt16;
            Graphics::PixelRect scissor;
            std::uint32_t firstIndex = 0;
            std::uint32_t indexCount = 0;
            std::int32_t baseVertex = 0;
        };

        /** @brief The pipeline `description` draws with into `format`; null when it cannot be made (logged once). */
        [[nodiscard]] const PipelineEntry* Pipeline(const PipelineDescription2D& description,
                                                    Graphics::TextureFormat format);
        /** @brief Resolves one command, appending its draw; false (logged once a reason) when it is skipped. */
        bool Resolve(const DrawList2D& list, const Command2D& command, std::size_t vertexOffset,
                     std::size_t indexOffset, Graphics::TextureFormat format, Graphics::PixelSize pixels, float scale,
                     std::vector<Draw>& draws);
        /** @brief Logs `message` once for `reason`. */
        void LogOnce(const std::string& reason, const std::string& message);
        void DestroyPipelinesOf(AssetKey shader);

        Graphics::GraphicsDevice& m_device;
        ILogger& m_logger;
        SceneRenderer& m_images;

        std::map<AssetKey, ShaderEntry> m_shaders;
        std::vector<PipelineEntry> m_pipelines;

        Graphics::BindingGroupLayoutHandle m_targetLayout;
        std::unique_ptr<BlockRing> m_targetBlocks;
        std::unique_ptr<ByteRing> m_vertices;
        std::unique_ptr<ByteRing> m_indices;
        std::unique_ptr<ByteRing> m_parameters;
        /** @brief The binding groups the last frame made, destroyed (deferred by the device) as the next begins. */
        std::vector<Graphics::BindingGroupHandle> m_frameGroups;
        /** @name This frame's set 1 groups as they are resolved, and its parameter blocks
         * @{
         */
        std::vector<Graphics::BindingGroupDesc> m_pendingGroups;
        std::vector<std::byte> m_parameterBytes;
        /** @brief Each parameter block's group and entry, given the frame's parameter buffer once it is written. */
        std::vector<std::pair<std::size_t, std::size_t>> m_parameterEntries;
        /** @} */

        OverlayFrame m_frame;
        std::set<std::string> m_logged;
    };
} // namespace Fenrir::Render
