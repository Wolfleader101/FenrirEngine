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

    /** @brief An image 2D target's lists for one frame, in submission order, and the texture they draw into. */
    struct ImageSubmission2D
    {
        /** @brief The image asset, so a list sampling its own image can be told apart. */
        AssetKey image;
        /** @brief An RGBA8 texture, sRGB or linear, with colour attachment and sampled usage. */
        Graphics::TextureHandle texture;
        /** @brief The texture's size and format. */
        Graphics::TextureDesc desc;
        /** @brief The lists drawn into it, in submission order. */
        std::vector<DrawList2D> lists;
    };

    /** @brief What the 2D render feature draws in one frame: plain data the facade copies in, nothing kept after it. */
    struct Frame2D
    {
        /** @brief The window overlay's lists, in submission order. */
        std::vector<Submission2D> submissions;
        /** @brief The window's pixels per logical unit. */
        float contentScale = 1.0f;
        /**
         * @brief The texture the overlay draws into instead of the window's backbuffer, with its description; empty
         * for the window. The facade always draws into the window; the core's tests name an offscreen target.
         */
        Graphics::TextureHandle target;
        Graphics::TextureDesc targetDesc;
        /** @brief The image 2D targets, in the order of their first submission, each drawn before the overlay. */
        std::vector<ImageSubmission2D> images;
    };

    /** @brief What the 2D render feature drew in its last frame, for RendererStats. */
    struct Stats2D
    {
        /** @brief Lists drawn, on the overlay and into images; a skipped list is not counted. */
        std::uint32_t lists = 0;
        /** @brief The commands of those lists, of every kind. */
        std::uint32_t commands = 0;
        /** @brief Offscreen groups drawn into a transient image. */
        std::uint32_t groups = 0;
        /** @brief The bytes of those groups' transient images. */
        std::uint32_t transientImageBytes = 0;
    };

    /**
     * @brief The 2D render feature (ADR-0027, ADR-0060): draws 2D draw lists into 2D targets. Two are built: the window
     * overlay, one `Overlay 2D` pass after every view's Output, before the debug feature's Overlay pass, so debug
     * screen text stays the fixed top layer; and the image 2D target, one `Image 2D` pass per image, drawn before the
     * overlay and before any view that samples the image (the graph orders them, an imported texture being one resource
     * however many features import it). A frame with nothing submitted adds no pass, nor does an overlay whose lists
     * draw nothing.
     *
     * **A target** maps local logical coordinates (top-left origin, Y down) to clip space through set 0 of every 2D
     * shader (`Fenrir/2D.glsl`): over the window's logical size, its pixel size divided by the content scale, or over
     * an image's pixels at a content scale of 1. There is no depth and no culling. A command's scissor is scaled by the
     * content scale, rounded outwards to whole pixels and clamped to the target; one that covers no pixel draws
     * nothing. An image 2D target is cleared to transparent before its lists draw.
     *
     * **Order:** the images in the frame's order, each image's lists in order; then the overlay's layers lowest first,
     * then submission order; then each list's command order.
     *
     * **Offscreen groups.** A group's commands draw into a transient image from the graph's pool (keyed by size and
     * format, so equal ones with disjoint lifetimes share a texture): sRGB RGBA8, premultiplied, covering the group's
     * bounds in the target's pixels rounded outwards and clamped to the enclosing group's or the target's, cleared to
     * transparent. It is a target of its own, a `Group 2D` pass whose local coordinates are the list's offset to the
     * group's origin, at the target's content scale, its scissors resolved as the target's and clamped to it. The
     * end-group command then draws the image over the enclosing target as one quad, through builtin:2d's textured path
     * with the composite's opacity and blend mode (`2D composite` at set 1), so an inner group's pass runs before its
     * outer's. A group with nothing drawn in it, or covering no pixel, draws nothing and takes no image. Groups nest to
     * MaxGroupDepth2D; a deeper one is logged once and its commands draw into the group around it. FenrirDraw2D
     * declares filters and masks, which are not realised yet: a filter is logged once and its group drawn plainly; a
     * mask group is logged once and draws nothing, and the commands it would mask draw unmasked.
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
     * bound with its slot's sampler. A missing, stale or unprepared image binds the texture's parameter default, as
     * does an image 2D target's own image sampled by its own lists (logged once).
     *
     * **Skipped,** each reason logged once: a list whose groups and masks do not balance, whole; a command with more
     * than MaxTextureSlots2D texture slots or MaxParameterBytes2D parameter bytes, a pipeline index or an index range
     * outside its list, or a shader that is stale or not a 2D shader.
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
        void SetFrame(Frame2D frame);

        /** @brief What the last AddPasses drew. */
        [[nodiscard]] Stats2D Stats() const noexcept
        {
            return m_stats;
        }

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

        /** @brief One command resolved for recording, or one group's image composited. */
        struct Draw
        {
            Graphics::PipelineHandle pipeline;
            /** @brief Its set 1 group: one more than its place in the frame's pending groups, 0 for none. */
            std::size_t groupIndex = 0;
            Graphics::BindingGroupHandle group;
            /** @brief A composite's group image, bound at set 1 as the pass is recorded; empty for a command. */
            GraphResource composite;
            std::size_t vertexOffset = 0;
            std::size_t indexOffset = 0;
            Graphics::IndexFormat indexFormat = Graphics::IndexFormat::UInt16;
            Graphics::PixelRect scissor;
            std::uint32_t firstIndex = 0;
            std::uint32_t indexCount = 0;
            std::int32_t baseVertex = 0;
        };

        /** @brief One pass of the frame: a 2D target (the overlay, an image, or a group's image) and its draws. */
        struct TargetPass
        {
            /** @brief The pass's label: `Overlay 2D`, `Image 2D` or `Group 2D`. */
            std::string name;
            /** @brief Empty for a group until it ends with something drawn in it. */
            GraphResource target;
            /** @brief Load for the overlay, which draws over the views; clear to transparent for an image. */
            Graphics::LoadOp load = Graphics::LoadOp::Clear;
            Graphics::TextureFormat format = Graphics::TextureFormat::Rgba8Srgb;
            /** @brief The target's size: the window's, the image's, or the group's bounds in pixels. */
            Graphics::PixelSize pixels;
            /** @brief Its top-left in its root target's pixels: a group's place there, 0 for a target. */
            std::int32_t originX = 0;
            std::int32_t originY = 0;
            /** @brief Pixels per local unit: the window's content scale, 1 on an image, a group's target's. */
            float scale = 1.0f;
            std::vector<Draw> draws;
            /** @brief The group images and earlier image 2D targets its draws sample, whose passes run first. */
            std::vector<GraphResource> samples;
        };

        /** @brief What one list draws into: its root pass, and the images it may sample. */
        struct ListTarget
        {
            /** @brief The root pass, by index in m_passes. */
            std::size_t root = 0;
            /** @brief An image 2D target's own image, which its lists may not sample; empty on the overlay. */
            AssetKey self;
            /** @brief The image 2D targets drawn earlier this frame, by image. */
            const std::map<AssetKey, GraphResource>* images = nullptr;
        };

        /** @brief builtin:2d's keyless variant, which composites a group's premultiplied image. */
        struct Composite
        {
            std::unique_ptr<ShaderContainer> container;
            const ShaderVariant* variant = nullptr;
            Graphics::BindingGroupLayoutHandle layout;
            std::map<std::pair<GroupBlend2D, Graphics::TextureFormat>, Graphics::PipelineHandle> pipelines;
        };

        /** @brief The pipeline `description` draws with into `format`; null when it cannot be made (logged once). */
        [[nodiscard]] const PipelineEntry* Pipeline(const PipelineDescription2D& description,
                                                    Graphics::TextureFormat format);
        /** @brief The pipeline a group's image composites with by `blend` into `format`; empty if it cannot be made. */
        [[nodiscard]] Graphics::PipelineHandle CompositePipeline(GroupBlend2D blend, Graphics::TextureFormat format);
        /**
         * @brief Draws `list`'s commands into `target`'s root pass and the group passes its groups open, adding its
         * bytes to the frame's; false (logged once) when its groups do not balance and it is skipped.
         */
        bool DrawList(const DrawList2D& list, const ListTarget& target, RenderGraph& graph);
        /** @brief Resolves one command into `pass`, appending its draw; false (logged once a reason) when skipped. */
        bool Resolve(const DrawList2D& list, const DrawCommand2D& command, std::size_t vertexOffset,
                     std::size_t indexOffset, const ListTarget& target, TargetPass& pass);
        /** @brief Appends the quad that draws `group`'s image over `into` as `composite` says. */
        void AddComposite(const TargetPass& group, const EndGroup2D& composite, TargetPass& into);
        /** @brief A set 1 group binding `image` for a composite, made as its pass is recorded, freed next frame. */
        [[nodiscard]] Graphics::BindingGroupHandle CompositeGroup(Graphics::TextureHandle image);
        /** @brief Logs `message` once for `reason`. */
        void LogOnce(const std::string& reason, const std::string& message);
        void DestroyPipelinesOf(AssetKey shader);

        Graphics::GraphicsDevice& m_device;
        ILogger& m_logger;
        SceneRenderer& m_images;

        std::map<AssetKey, ShaderEntry> m_shaders;
        std::vector<PipelineEntry> m_pipelines;
        Composite m_composite;

        Graphics::BindingGroupLayoutHandle m_targetLayout;
        std::unique_ptr<BlockRing> m_targetBlocks;
        std::unique_ptr<ByteRing> m_vertices;
        std::unique_ptr<ByteRing> m_indices;
        std::unique_ptr<ByteRing> m_parameters;
        /** @brief The binding groups the last frame made, destroyed (deferred by the device) as the next begins. */
        std::vector<Graphics::BindingGroupHandle> m_frameGroups;
        /** @name This frame's passes, bytes and set 1 groups as they are resolved, and its parameter blocks
         * @{
         */
        std::vector<TargetPass> m_passes;
        /** @brief The passes to add, by index in m_passes, in the order they finished: a group before its target. */
        std::vector<std::size_t> m_finished;
        std::vector<std::byte> m_vertexBytes;
        std::vector<std::byte> m_indexBytes;
        std::vector<Graphics::BindingGroupDesc> m_pendingGroups;
        std::vector<std::byte> m_parameterBytes;
        /** @brief Each parameter block's group and entry, given the frame's parameter buffer once it is written. */
        std::vector<std::pair<std::size_t, std::size_t>> m_parameterEntries;
        /** @} */

        Frame2D m_frame;
        Stats2D m_stats;
        std::set<std::string> m_logged;
    };
} // namespace Fenrir::Render
