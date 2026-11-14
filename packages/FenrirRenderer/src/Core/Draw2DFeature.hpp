#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
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

    /**
     * @brief A world-space 2D target's lists for one frame (ADR-0060): a quad in the world, and the lists drawn on it
     * in submission order.
     */
    struct WorldSubmission2D
    {
        /**
         * @brief From the lists' local units (top-left origin, Y down) to world space: the quad's world matrix and its
         * scale and offset.
         */
        Math::Mat4 localToWorld{1.0f};
        /** @brief The quad's size in the lists' local units. */
        Math::Vec2 logicalSize{0.0f};
        /** @brief The world point it sorts with the transparent items by: its entity's origin. */
        Math::Vec3 origin{0.0f};
        std::vector<DrawList2D> lists;
    };

    /** @brief A filter's radius, in pixels, past which its blur runs at half the image's resolution (ADR-0060). */
    inline constexpr float HalfResolutionBlur2D = 8.0f;
    /** @brief The most taps a pass of a filter's blur takes on each side of the centre; wider blurs space them out. */
    inline constexpr int MaxBlurTaps2D = 32;

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
        /** @brief The world-space 2D targets, drawn in each view's Transparent pass through SceneRenderer. */
        std::vector<WorldSubmission2D> worlds;
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
     * MaxGroupDepth2D; a deeper one is logged once and its commands draw into the group around it (a deeper mask's
     * shape draws nothing, and what it reaches draws unmasked).
     *
     * **Filters** draw through builtin:2d's BLUR and SHADOW variants (ADR-0060). A radius is a Gaussian's standard
     * deviation, in local units times the target's scale in pixels; a blurred or shadowed group's image reaches three
     * of them past its bounds, so the blur spreads. The blur is separable: a `Blur 2D` pass blurs the image across into
     * one more transient image, at half its resolution when the radius passes HalfResolutionBlur2D pixels, and the
     * composite blurs it down as it draws it, so each blur is two passes of the Gaussian, at most MaxBlurTaps2D taps a
     * side apart (wider ones space their taps out). `Blur` composites the blurred image at the group's opacity and
     * blend. `DropShadow` draws the blurred image's alpha tinted by the shadow colour (times the opacity) at the offset
     * first, then the image as `None` does. `BackdropBlur` copies what the enclosing target holds under the group's
     * bounds, and three radii about them, into a transient image (a `Backdrop 2D` copy pass, ending the target's pass
     * there and going on in another that loads it), blurs it as a group's image is, but leaving out the taps past the
     * copy and weighing the rest up to make up for them, and draws it back over the bounds in place of what was there;
     * then the group's image, if anything was drawn in it, as `None` does. A radius of 0 or less is no filter.
     *
     * **Masks.** A mask group's image is not drawn: the draws after it, up to its EndMask2D, draw through their
     * shader's `MASK` variant, whose `mask` texture binds the image and whose `maskRect` parameter is the image's
     * top-left in local units and one over its size in them; the shader multiplies its colour by the image's alpha
     * there, and by nothing outside it. A shader with no `MASK` variant draws them unmasked (logged once). Only the
     * innermost mask applies. A mask with nothing drawn in it, or covering no pixel, masks everything away. Composites
     * a mask reaches are not masked, but the draws inside their groups are.
     *
     * **Groups off** (SetOffscreenGroups): a group's commands draw flat into the target around it and its composite
     * and filter are ignored; a mask's shape draws nothing and masks nothing.
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
    class Draw2DFeature final : public RenderFeature, public WorldQuads
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

        /**
         * @brief Whether offscreen groups are realised (RendererConfig::offscreenGroups2D, on by default). Off, a
         * group's commands draw flat into the target around it, its composite ignored, and a mask's shape draws nothing
         * and masks nothing.
         */
        void SetOffscreenGroups(bool on) noexcept
        {
            m_offscreenGroups = on;
        }

        /** @brief What the last AddPasses drew. */
        [[nodiscard]] Stats2D Stats() const noexcept
        {
            return m_stats;
        }

        void AddPasses(RenderGraph& graph, const FrameContext& frame) override;

        /** @name The world-space 2D target (ADR-0060), which the scene renderer's views draw (WorldQuads)
         * Each WorldSubmission2D is a `World 2D` target of its own: local units at a content scale of 1 over the
         * quad's logical size, with no hardware scissor (a command's is ignored) and no pass of its own. Its draws
         * are recorded into a view's Transparent pass, depth tested without writing depth, into the view's scene
         * colour at its samples; a Gamma blend space draws as Linear (logged). Its groups draw into transient images
         * in the Transparent stage, before the views that composite them; a backdrop filter on the quad itself reads
         * nothing, logged once, and its group draws without one. Its lists sample image 2D targets as they were
         * before this frame. A quad whose lists draw nothing is not given to the views.
         * @{
         */
        std::vector<Quad> ResolveWorldQuads(RenderGraph& graph, const FrameContext& frame,
                                            Graphics::TextureFormat colour, std::uint32_t samples,
                                            std::size_t views) override;
        [[nodiscard]] std::vector<GraphResource> WorldQuadSamples(std::size_t quad) const override;
        [[nodiscard]] Graphics::BindingGroupHandle WorldQuadTarget(std::uint64_t frameIndex, std::size_t quad,
                                                                   std::size_t view,
                                                                   const Math::Mat4& viewProjection) override;
        void RecordWorldQuad(PassContext& context, std::size_t quad, Graphics::BindingGroupHandle target,
                             Graphics::PixelRect viewport) override;
        /** @} */

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

        /** @brief What a 2D pass draws into, as a pipeline is made for it. */
        struct PassFormat
        {
            Graphics::TextureFormat format = Graphics::TextureFormat::Rgba8Srgb;
            std::uint32_t samples = 1;
            /** @brief A world quad's: depth tested against the view's Depth32Float depth, without writing it. */
            bool world = false;

            friend auto operator<=>(const PassFormat&, const PassFormat&) = default;
        };

        /** @brief A realised pipeline: what it was made for, and what it draws with. */
        struct PipelineEntry
        {
            PipelineDescription2D description;
            PassFormat format;
            /** @brief Whether it is the shader's `MASK` variant, for draws a mask reaches. */
            bool masked = false;
            Graphics::PipelineHandle pipeline;
            const ShaderVariant* variant = nullptr;
            Graphics::BindingGroupLayoutHandle layout;
        };

        /** @brief Which of builtin:2d's variants a composite draws with. */
        enum class CompositeKind : std::uint8_t
        {
            /** @brief The keyless variant: the image times the vertex colour. */
            Plain,
            /** @brief BLUR: one pass of a Gaussian, times the vertex colour. */
            Blur,
            /** @brief BLUR and SHADOW: one pass of a Gaussian of the image's alpha, tinted. */
            Shadow,
        };

        /** @brief One command resolved for recording, or one image composited, blurred or copied. */
        struct Draw
        {
            Graphics::PipelineHandle pipeline;
            /** @brief Its set 1 group: one more than its place in the frame's pending groups, 0 for none. */
            std::size_t groupIndex = 0;
            Graphics::BindingGroupHandle group;
            /** @brief A composite's source image, bound at set 1 as the pass is recorded; empty for a command. */
            GraphResource composite;
            CompositeKind compositeKind = CompositeKind::Plain;
            /** @brief A Blur or Shadow composite's parameter block, by offset in the frame's parameter bytes. */
            std::size_t compositeParameters = 0;
            /** @brief The mask image a command a mask reaches binds, as its pass is recorded; empty for none. */
            GraphResource mask;
            /** @brief The `mask` texture's entry in its set 1 group, and that group, made as the pass is recorded. */
            std::size_t maskEntry = 0;
            Graphics::BindingGroupDesc maskedGroup;
            std::size_t vertexOffset = 0;
            std::size_t indexOffset = 0;
            Graphics::IndexFormat indexFormat = Graphics::IndexFormat::UInt16;
            Graphics::PixelRect scissor;
            std::uint32_t firstIndex = 0;
            std::uint32_t indexCount = 0;
            std::int32_t baseVertex = 0;
        };

        /** @brief What a `Backdrop 2D` pass copies into its target: a rectangle of `source`'s pixels. */
        struct Copy
        {
            GraphResource source;
            Graphics::PixelRect region;
        };

        /**
         * @brief One pass of the frame: a 2D target (the overlay, an image, or a group's image) and its draws, a blur
         * of an image, or a backdrop's copy.
         */
        struct TargetPass
        {
            /** @brief The pass's label: `Overlay 2D`, `Image 2D`, `World 2D`, `Group 2D`, `Blur 2D` or `Backdrop 2D`.
             */
            std::string name;
            /** @brief The stage it is added in: `Overlay`, or `Transparent` for a world quad's groups. */
            RenderStage stage = RenderStage::Overlay;
            /** @brief A world quad's root: recorded into views' Transparent passes, never added as a pass. */
            bool world = false;
            /** @brief Its MSAA samples: a world quad's are its view's, 1 for every other pass. */
            std::uint32_t sampleCount = 1;
            /** @brief Empty for a group until it ends with something drawn in it, or a backdrop reads it. */
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
            /** @brief A `Backdrop 2D` pass's copy, which it does in place of drawing. */
            std::optional<Copy> copy;
        };

        /** @brief What one list draws into: its root pass, and the images it may sample. */
        struct ListTarget
        {
            /** @brief The root pass, by index in m_passes: after a backdrop, the pass that goes on drawing into it. */
            std::size_t root = 0;
            /** @brief An image 2D target's own image, which its lists may not sample; empty on the overlay. */
            AssetKey self;
            /** @brief The image 2D targets drawn earlier this frame, by image. */
            const std::map<AssetKey, GraphResource>* images = nullptr;
        };

        /** @brief What a command a mask reaches is drawn through. */
        struct MaskState
        {
            enum class Kind : std::uint8_t
            {
                /** @brief The mask's image. */
                Image,
                /** @brief Nothing: a mask with nothing drawn in it, or covering no pixel, masks everything away. */
                Nothing,
                /** @brief Not masked: groups are off, or the mask was too deep. */
                Unmasked,
            };
            Kind kind = Kind::Unmasked;
            GraphResource image;
            /** @brief The image's top-left in local units, then one over its size in them: the shader's `maskRect`. */
            Math::Vec4 rect{0.0f};
        };

        /** @brief builtin:2d's variants that composite, blur and tint a group's premultiplied image. */
        struct Composite
        {
            struct Variant
            {
                const ShaderVariant* variant = nullptr;
                Graphics::BindingGroupLayoutHandle layout;
            };
            std::unique_ptr<ShaderContainer> container;
            /** @brief By CompositeKind. */
            std::array<Variant, 3> variants;
            std::map<std::tuple<CompositeKind, Graphics::BlendMode, PassFormat>, Graphics::PipelineHandle> pipelines;
        };

        /** @brief One quad an image is drawn with: its corners in its pass's local units and its UV rectangle. */
        struct ImageQuad
        {
            float left = 0.0f;
            float top = 0.0f;
            float right = 0.0f;
            float bottom = 0.0f;
            Math::Vec4 uv{0.0f, 0.0f, 1.0f, 1.0f};
            /** @brief Its linear premultiplied vertex colour, which scales what it draws. */
            std::uint32_t colour = 0xFFFFFFFFu;
        };

        /** @brief The parameter block of builtin:2d's BLUR variants, std140. */
        struct BlurBlock
        {
            Math::Vec4 blur{0.0f};
            Math::Vec4 bounds{0.0f, 0.0f, 1.0f, 1.0f};
            Math::Vec4 options{0.0f};
            Math::Vec4 shadow{0.0f};
        };

        /** @brief An image blurred across by a `Blur 2D` pass, and its size. */
        struct Blurred
        {
            GraphResource image;
            Graphics::PixelSize pixels;
        };

        /**
         * @brief The pipeline `description` draws with into `format`, its `MASK` variant when `masked`; null when it
         * cannot be made (logged once).
         */
        [[nodiscard]] const PipelineEntry* Pipeline(const PipelineDescription2D& description, const PassFormat& format,
                                                    bool masked);
        /** @brief The pipeline a composite of `kind` draws with by `blend` into `format`; empty if it cannot be made.
         */
        [[nodiscard]] Graphics::PipelineHandle CompositePipeline(CompositeKind kind, Graphics::BlendMode blend,
                                                                 const PassFormat& format);
        /** @brief What `pass` draws into. */
        [[nodiscard]] static PassFormat FormatOf(const TargetPass& pass) noexcept
        {
            return {pass.format, pass.sampleCount, pass.world};
        }
        /**
         * @brief Starts frame `frameIndex` once, from whichever of AddPasses and ResolveWorldQuads comes first: frees
         * the last frame's groups and takes the frame SetFrame gave.
         */
        void BeginFrame(std::uint64_t frameIndex);
        /** @brief Records `draws` with `target` at set 0, each scissored to `scissor` or, when it has none, its own. */
        void RecordDraws(PassContext& context, const std::vector<Draw>& draws, Graphics::BindingGroupHandle target,
                         const std::optional<Graphics::PixelRect>& scissor);
        /**
         * @brief Draws `list`'s commands into `target`'s root pass and the group passes its groups open, adding its
         * bytes to the frame's; false (logged once) when its groups do not balance and it is skipped. A backdrop in
         * the root moves `target.root` on to the pass that goes on drawing into it.
         */
        bool DrawList(const DrawList2D& list, ListTarget& target, RenderGraph& graph);
        /**
         * @brief Resolves one command into `pass`, appending its draw, masked as `mask` says (none: unmasked); false
         * (logged once a reason) when skipped.
         */
        bool Resolve(const DrawList2D& list, const DrawCommand2D& command, std::size_t vertexOffset,
                     std::size_t indexOffset, const ListTarget& target, const MaskState* mask, TargetPass& pass);
        /**
         * @brief Appends a quad drawing `source` into `into` through the composite `kind` by `blend`, with `block` as
         * its parameters when it blurs.
         */
        void AddImageQuad(TargetPass& into, const ImageQuad& quad, GraphResource source, CompositeKind kind,
                          Graphics::BlendMode blend, const BlurBlock* block = nullptr);
        /** @brief The quad covering the pixels of `group` (a group's pass) in `into`'s local units, moved `offset`. */
        [[nodiscard]] static ImageQuad QuadOver(const TargetPass& group, const TargetPass& into,
                                                Math::Vec2 offset = Math::Vec2(0.0f));
        /** @brief A transient image of `pixels` and `format` that 2D passes draw into, counted in the stats. */
        [[nodiscard]] GraphResource TransientImage(RenderGraph& graph, const char* name, Graphics::PixelSize pixels,
                                                   Graphics::TextureFormat format);
        /**
         * @brief Adds a `Blur 2D` pass blurring `source` (of `pixels`, its UV rectangle `bounds` blurred) across by
         * `sigma` of its pixels into a new transient image, at half the size past HalfResolutionBlur2D; with `clamp`
         * the taps past `bounds` are left out and the rest weighed up, else they read transparent.
         */
        [[nodiscard]] Blurred BlurAcross(RenderGraph& graph, GraphResource source, Graphics::PixelSize pixels,
                                         Math::Vec4 bounds, float sigma, Graphics::TextureFormat format, bool clamp);
        /**
         * @brief The block of the composite that blurs `blurred` down by `sigma` pixels of the image it was blurred
         * from, `height` pixels high.
         */
        [[nodiscard]] static BlurBlock BlurDown(const Blurred& blurred, std::uint32_t height, float sigma, bool clamp);
        /**
         * @brief Draws a backdrop blur of `sigma` pixels under `group` in the pass `into`: ends that pass, copies and
         * blurs what it holds there, and returns the pass that goes on drawing into its target, the blurred backdrop
         * drawn back first.
         */
        [[nodiscard]] std::size_t AddBackdrop(RenderGraph& graph, std::size_t group, std::size_t into, float sigma);
        /**
         * @brief A set 1 group for a composite of `kind` binding `image` and, when it blurs, the parameter block at
         * `parameters` in `buffer`; made as its pass is recorded, freed next frame.
         */
        [[nodiscard]] Graphics::BindingGroupHandle CompositeGroup(CompositeKind kind, Graphics::TextureHandle image,
                                                                  Graphics::BufferHandle buffer,
                                                                  std::size_t parameters);
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
        /** @brief Whether each pending group is made as its pass is recorded: a masked draw's, binding the mask. */
        std::vector<bool> m_deferredGroups;
        std::vector<std::byte> m_parameterBytes;
        /** @brief Each parameter block's group and entry, given the frame's parameter buffer once it is written. */
        std::vector<std::pair<std::size_t, std::size_t>> m_parameterEntries;
        /** @} */

        /** @brief One world quad given to the views: its root pass, its matrix and its size in local units. */
        struct WorldQuad
        {
            std::size_t pass = 0;
            Math::Mat4 localToWorld{1.0f};
            Math::Vec2 logicalSize{0.0f};
        };

        Frame2D m_frame;
        /** @brief The frame being drawn, taken from m_frame by BeginFrame. */
        Frame2D m_drawn;
        /** @brief The frame BeginFrame last started; none before the first. */
        std::optional<std::uint64_t> m_begun;
        /** @brief Whether AddPasses ran in that frame: world quads resolved after it would draw nothing. */
        bool m_passesAdded = false;
        /** @brief The stage the passes made while resolving a list are added in. */
        RenderStage m_listStage = RenderStage::Overlay;
        std::vector<WorldQuad> m_worlds;
        /** @brief The world quads' set 0 blocks: one per quad and view. */
        std::unique_ptr<BlockRing> m_worldBlocks;
        /** @brief The frame's vertex, index and parameter buffers, once AddPasses has written them. */
        Graphics::BufferHandle m_vertexBuffer;
        Graphics::BufferHandle m_indexBuffer;
        Graphics::BufferHandle m_parameterBuffer;
        Stats2D m_stats;
        bool m_offscreenGroups = true;
        std::set<std::string> m_logged;
    };
} // namespace Fenrir::Render
