#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Core/BlockRing.hpp"
#include "Core/RenderFeature.hpp"
#include "Core/SceneFrame.hpp"
#include "Device/GraphicsDevice.hpp"
#include "FenrirAssets/ImageData.hpp"
#include "FenrirAssets/MaterialData.hpp"
#include "FenrirAssets/MeshData.hpp"
#include "FenrirAssets/ShaderContainer.hpp"
#include "FenrirAssets/ShaderData.hpp"

namespace Fenrir
{
    class ILogger;
    struct RendererProbe;
} // namespace Fenrir

namespace Fenrir::Render
{
    /** @brief What the scene renderer did since it was last asked. */
    struct SceneStats
    {
        /** @brief Draws of draw-list items in the last frame, one per item per view. */
        std::uint32_t draws = 0;
        /** @brief Meshes, images and material blocks uploaded since the last call. */
        std::uint32_t uploads = 0;
        /** @brief Bytes written into the last frame's palette buffer. */
        std::uint32_t paletteBytes = 0;
        /** @brief Skeleton palettes the last frame's palette buffer held (SceneFrame::palettes). */
        std::uint32_t palettes = 0;
        /** @brief Skinned draws in the last frame, one per item per view. */
        std::uint32_t skinnedDraws = 0;
    };

    /** @brief A view the scene renderer added passes for this frame, for render features that draw into it. */
    struct DrawnView
    {
        /** @brief Its place in SceneFrame::views. */
        std::size_t view = 0;
        /** @brief Its scene targets, which its Output pass reads after every earlier stage. */
        GraphResource sceneColour;
        GraphResource sceneDepth;
        /** @brief Its view block's binding group this frame: binding set 1, as `Fenrir/View.glsl` declares it. */
        Graphics::BindingGroupHandle viewGroup;
    };

    /**
     * @brief The render feature that draws the scene (ADR-0019, ADR-0023, ADR-0024): the GPU side of the meshes,
     * shaders and materials the facade prepares, and each frame's draw list and views.
     *
     * **Prepare.** The facade hands it `(key, revision, data)` for each asset that is new or changed (IsCurrent says
     * which), and releases the key of each asset that went stale. GPU resources are kept by key and revision; a new
     * revision remakes them, and a material follows a new revision of its shader.
     *
     * **Each frame** (AddPasses, after SetFrame): the draws are resolved to pipelines, made lazily and cached by
     * shader variant, render state (blend mode and culling), vertex layout and sample count while the shader lives,
     * and sorted by a 64-bit key (render stage in the high bits; then pipeline, material and mesh). One render graph
     * holds every view (ADR-0019). Each view gets transient `SceneColor` (linear RGBA16F) and `SceneDepth` (32-bit
     * float, reversed-Z, cleared to 0) at its viewport's size with the configured MSAA samples, and its passes:
     * `Opaque`, one draw per `Opaque` or `Masked` item, writing depth; `Background` at depth 0, the view's colour or
     * its Sky cubemap (sampled by the direction each pixel looks along); `Transparent`, when there are `AlphaBlend` or
     * `Additive` items, depth tested without writing, drawn back to front by the view-space depth of each item's world
     * origin (no order-independent transparency, so intersecting or nested transparent items can sort wrongly); and
     * `Output`, which resolves SceneColor and clamps it into its viewport of its render target, the window's backbuffer
     * or an image. The first view into a target clears it to black. With no view the backbuffer is cleared to black.
     *
     * **Variants** (ADR-0024): the renderer picks the keys. A `Masked` material draws with its shader's `ALPHA_MASK`
     * variant if the shader has the key, its `MaterialData::alphaCutoff` written into the variant's `alphaCutoff` block
     * member. A skinned draw (ADR-0035) draws with the `SKINNED` variant; a shader without the key draws it with the
     * fallback material's, logged once per shader as a warning. Every other key stays off.
     *
     * **Skinning** (ADR-0035): each frame's palettes are written into the frame's palette buffer, set 3 binding 1, as
     * `GpuBone`s, ring-buffered per frame in flight and grown by doubling, with nothing kept across frames. A skinned
     * draw's record carries its `paletteOffset`; one whose palette range (`paletteOffset` plus its mesh's bone count)
     * passes the buffer's capacity is an internal error and is not drawn. Every record also carries the sign of its
     * world matrix's determinant, for the shaders' tangent handedness.
     *
     * **Images** with pixels that materials name are prepared like assets: uploaded as 2D textures (RGBA8, sRGB or
     * linear as the image is, RGBA16F or RGBA32F) with every mip level made at upload, and bound with their texture
     * parameter's sampler, one sampler per distinct description.
     *
     * **Render target images** (ADR-0026) are prepared like assets, as RGBA8 sRGB textures of their image's size. A
     * material texture parameter naming one binds it, and a view whose draws sample an image another view draws is
     * ordered after it by the graph. A view whose draws sample its own target, or an image whose views would have to
     * wait on it (a cycle, broken in the views' order), gets the parameter's default texture there, logged once.
     *
     * **Bindings.** Sets 0 (frame), 1 (view) and 3 (draw) are declared here as `Fenrir/Frame.glsl`, `View.glsl` and
     * `Draw.glsl` declare them; in debug builds a shader whose reflection disagrees fails pipeline creation with a
     * diagnostic. Set 2, the material, is laid out from the shader container's reflection: its `MaterialParams` block
     * is the default block image with the material's parameter overrides written over it, and each texture binds the
     * renderer's default texture its parameter default names. A vertex stream the shader reads but the mesh lacks binds
     * a constant default (normal +Y, UV 0, colour white), logged once per mesh and stream; tangents, which are
     * optional (ADR-0048), bind the zero tangent unlogged, its handedness 0 telling the shader there are none.
     */
    class SceneRenderer final : public RenderFeature
    {
      public:
        /**
         * @brief Makes the default textures, the fixed sets' layouts and the internal pipelines on `device`, for scene
         * targets of `samples` samples per pixel (1: no MSAA).
         */
        SceneRenderer(Graphics::GraphicsDevice& device, ILogger& logger, std::uint32_t samples = 1);
        ~SceneRenderer() override;

        SceneRenderer(const SceneRenderer&) = delete;
        SceneRenderer& operator=(const SceneRenderer&) = delete;
        SceneRenderer(SceneRenderer&&) = delete;
        SceneRenderer& operator=(SceneRenderer&&) = delete;

        /** @name Prepare
         * Is…Current says whether the GPU side of `key` is at `revision`; Prepare… (re)makes it from the data, and
         * Release… frees it.
         * @{
         */
        [[nodiscard]] bool IsShaderCurrent(AssetKey key, std::uint32_t revision) const;
        /** @brief A shader container that does not read, or is a 2D shader, is logged and draws nothing. */
        void PrepareShader(AssetKey key, std::uint32_t revision, const ShaderData& shader);
        void ReleaseShader(AssetKey key);

        [[nodiscard]] bool IsMeshCurrent(AssetKey key, std::uint32_t revision) const;
        void PrepareMesh(AssetKey key, std::uint32_t revision, const MeshData& mesh);
        void ReleaseMesh(AssetKey key);

        /** @brief Also false when the material's shader was prepared again since. */
        [[nodiscard]] bool IsMaterialCurrent(AssetKey key, std::uint32_t revision) const;
        /** @brief Prepare the material's shader first: a material whose shader is not prepared draws as the fallback.
         */
        void PrepareMaterial(AssetKey key, std::uint32_t revision, const MaterialData& material);
        void ReleaseMaterial(AssetKey key);

        [[nodiscard]] bool IsImageCurrent(AssetKey key, std::uint32_t revision) const;
        /** @brief A flat image with pixels, uploaded with its mips for materials to sample. */
        void PrepareImage(AssetKey key, std::uint32_t revision, const ImageData& image);
        void ReleaseImage(AssetKey key);

        [[nodiscard]] bool IsCubemapCurrent(AssetKey key, std::uint32_t revision) const;
        /**
         * @brief A cubemap image with pixels, for a Sky: uploaded as a cube texture with every mip level of each face
         * made at upload, and sampled by the Background pass's CUBEMAP variant.
         */
        void PrepareCubemap(AssetKey key, std::uint32_t revision, const ImageData& image);
        void ReleaseCubemap(AssetKey key);

        [[nodiscard]] bool IsTargetCurrent(AssetKey key, std::uint32_t revision) const;
        /** @brief A render target image: an RGBA8 sRGB texture of `size` that views draw into and materials sample. */
        void PrepareTarget(AssetKey key, std::uint32_t revision, Graphics::PixelSize size);
        void ReleaseTarget(AssetKey key);

        /** @brief The keys with GPU resources, for the facade to release those that went stale. */
        [[nodiscard]] std::vector<AssetKey> TargetKeys() const;
        [[nodiscard]] std::vector<AssetKey> ImageKeys() const;
        [[nodiscard]] std::vector<AssetKey> CubemapKeys() const;
        [[nodiscard]] std::vector<AssetKey> ShaderKeys() const;
        [[nodiscard]] std::vector<AssetKey> MeshKeys() const;
        [[nodiscard]] std::vector<AssetKey> MaterialKeys() const;
        /** @} */

        /** @brief What the next frame draws. */
        void SetFrame(SceneFrame frame);

        void AddPasses(RenderGraph& graph, const FrameContext& frame) override;

        /** @brief The last frame's draws, and the uploads since the last call. */
        [[nodiscard]] SceneStats TakeStats();

        /** @brief What SetFrame last gave it. */
        [[nodiscard]] const SceneFrame& Frame() const noexcept
        {
            return m_frame;
        }

        /** @brief The views the last AddPasses drew, in the order their passes were added. */
        [[nodiscard]] const std::vector<DrawnView>& DrawnViews() const noexcept
        {
            return m_drawnViews;
        }

        /** @brief The layout of the view block's binding groups (set 1). */
        [[nodiscard]] Graphics::BindingGroupLayoutHandle ViewLayout() const noexcept
        {
            return m_viewLayout;
        }

        /** @brief The scene targets' samples per pixel. */
        [[nodiscard]] std::uint32_t Samples() const noexcept
        {
            return m_samples;
        }

        /** @name Sampling, for render features that sample images as materials do
         * @{
         */
        /** @brief The texture the image or render target `key` is prepared as; empty if it is neither. */
        [[nodiscard]] Graphics::TextureHandle SampledTexture(AssetKey key) const;
        /** @brief The renderer's 1x1 default texture `texture`. */
        [[nodiscard]] Graphics::TextureHandle GetDefaultTexture(DefaultTexture texture) const;
        /** @brief A sampler for a texture's description, shared by equal descriptions. */
        [[nodiscard]] Graphics::SamplerHandle SamplerFor(const SamplerDescription& description);
        /** @} */

      private:
        friend struct Fenrir::RendererProbe;

        /** @brief One variant of a prepared shader, with set 2's layout from its reflection. */
        struct VariantEntry
        {
            const ShaderVariant* variant = nullptr;
            /** @brief Empty when the variant binds nothing in set 2. */
            Graphics::BindingGroupLayoutHandle materialLayout;
        };

        /** @brief A prepared shader: its container, and each variant with its set 2 layout. */
        struct ShaderEntry
        {
            std::uint32_t revision = 0;
            ShaderContainer container;
            /** @brief The base variant; null if the container has none. */
            const ShaderVariant* variant = nullptr;
            /** @brief The base variant's set 2 layout. */
            Graphics::BindingGroupLayoutHandle materialLayout;
            /** @brief Every variant, as the container lists them; the base variant first. */
            std::vector<VariantEntry> variants;
            /** @brief The number in sort keys, unique among live shaders. */
            std::uint32_t id = 0;
        };

        /** @brief A prepared image: its texture with every mip level. */
        struct ImageEntry
        {
            std::uint32_t revision = 0;
            Graphics::TextureHandle texture;
        };

        /** @brief A prepared Sky cubemap: its cube texture, and the Background pass's binding group sampling it. */
        struct CubemapEntry
        {
            std::uint32_t revision = 0;
            Graphics::TextureHandle texture;
            Graphics::BindingGroupHandle group;
        };

        /** @brief A prepared mesh: its streams back to back in one buffer, and its indices. */
        struct MeshEntry
        {
            std::uint32_t revision = 0;
            Graphics::BufferHandle vertices;
            Graphics::BufferHandle indices;
            std::uint32_t indexCount = 0;
            /** @brief Each standard stream's offset in `vertices`, by vertex location; empty if the mesh lacks it. */
            std::array<std::optional<std::size_t>, 8> streams{};
            std::uint32_t id = 0;
            /** @brief Whether the mesh has skin data, so its joints and weights are streams 6 and 7. */
            bool skinned = false;
            /** @brief How many palette entries a skinned draw of it reads, from its paletteOffset. */
            std::uint32_t boneCount = 0;
        };

        /** @brief A prepared render target image: its texture and size. */
        struct TargetEntry
        {
            std::uint32_t revision = 0;
            Graphics::TextureHandle texture;
            Graphics::PixelSize size;
        };

        /** @brief A material texture parameter naming an image, and what was bound for it. */
        struct SampledImage
        {
            /** @brief The parameter's index in MaterialEntry::bindings. */
            std::size_t binding = 0;
            AssetKey image;
            /** @brief The render target's or image's texture bound; empty when the image was not prepared. */
            Graphics::TextureHandle bound;
            /** @brief The parameter's default texture, bound in a view that may not sample the image. */
            Graphics::TextureHandle fallback;
        };

        /** @brief A prepared material: its block and binding group, for the shader revision it was laid out for. */
        struct MaterialEntry
        {
            std::uint32_t revision = 0;
            /** @brief The shader the material names, prepared or not. */
            AssetKey shaderKey;
            /** @brief Null when the material's shader is not prepared: it draws as the fallback. */
            const ShaderEntry* shader = nullptr;
            std::uint32_t shaderRevision = 0;
            /** @brief The shader's variant it draws with, picked from its blend mode. */
            const VariantEntry* variant = nullptr;
            /** @brief The variant it draws skinned items with, `SKINNED` added; null if the shader has no such key. */
            const VariantEntry* skinnedVariant = nullptr;
            BlendMode blendMode = BlendMode::Opaque;
            Graphics::BufferHandle block;
            Graphics::BindingGroupHandle group;
            /** @brief What `group` binds, to make it again with images replaced. */
            std::vector<Graphics::BindingGroupEntry> bindings;
            Graphics::BindingGroupLayoutHandle layout;
            /** @brief Its texture parameters that name an image. */
            std::vector<SampledImage> images;
            /** @brief Groups with some of its sampled targets replaced by defaults, by the replaced targets. */
            std::map<std::vector<AssetKey>, Graphics::BindingGroupHandle> withoutTargets;
            bool doubleSided = false;
            std::uint32_t id = 0;
        };

        /**
         * @brief What a mesh pipeline is made for: the program and variant, which read streams the mesh has, the render
         * state (blend mode and culling) and the scene targets' sample count.
         */
        struct PipelineKey
        {
            const ShaderEntry* shader = nullptr;
            const ShaderVariant* variant = nullptr;
            std::uint32_t streams = 0;
            BlendMode blend = BlendMode::Opaque;
            bool doubleSided = false;
            std::uint32_t samples = 1;

            friend auto operator<=>(const PipelineKey&, const PipelineKey&) = default;
        };

        /** @brief A made pipeline, or why it could not be made. */
        struct PipelineEntry
        {
            Graphics::PipelineHandle pipeline;
            /** @brief The vertex locations its slots feed, in slot order. */
            std::vector<std::uint32_t> locations;
            std::uint32_t id = 0;
            std::string failure;
        };

        /** @brief A draw resolved for recording, in draw-list order. */
        struct ResolvedDraw
        {
            std::uint64_t sortKey = 0;
            const PipelineEntry* pipeline = nullptr;
            /** @brief Null for the fallback material, which binds no group. */
            MaterialEntry* material = nullptr;
            const MeshEntry* mesh = nullptr;
            Math::Mat4 world{1.0f};
            /** @brief Drawn in the Transparent pass, not Opaque. */
            bool transparent = false;
            /** @brief A skinned draw's first palette entry, in bones; 0 for a rigid one. */
            std::uint32_t paletteOffset = 0;
            /** @brief Drawn skinned, from the palette. */
            bool skinned = false;
            /** @brief The world point a transparent draw is sorted by. */
            Math::Vec3 sortPoint{0.0f};
        };

        /** @brief One copy's draw set binding group (set 3), for the draw and palette buffers it was made with. */
        struct DrawGroup
        {
            Graphics::BufferHandle draws;
            Graphics::BufferHandle palette;
            Graphics::BindingGroupHandle group;
        };

        [[nodiscard]] const PipelineEntry& MeshPipeline(const ShaderEntry& shader, const VariantEntry& variant,
                                                        const MeshEntry& mesh, BlendMode blend, bool doubleSided);
        /** @brief Records `order`'s draws of `draws` into a pass, binding each one's pipeline, groups and streams. */
        void RecordDraws(Graphics::PassEncoder& encoder, const std::vector<ResolvedDraw>& draws,
                         const std::vector<Graphics::BindingGroupHandle>& groups, const std::vector<std::size_t>& order,
                         Graphics::BindingGroupHandle frameGroup, Graphics::BindingGroupHandle viewGroup,
                         Graphics::BindingGroupHandle drawGroup, Graphics::PixelRect viewport) const;
        [[nodiscard]] Graphics::PipelineHandle MakeInternalPipeline(
            const ShaderContainer& container, const ShaderVariant* variant, Graphics::BindingGroupLayoutHandle layout,
            Graphics::TextureFormat colour, bool depthTested, std::uint32_t samples,
            Graphics::BindingGroupLayoutHandle setOneLayout = {});
        /** @brief The material's group with the sampled targets in `replaced` bound to their defaults instead. */
        [[nodiscard]] Graphics::BindingGroupHandle GroupWithout(MaterialEntry& material,
                                                                const std::vector<AssetKey>& replaced);
        [[nodiscard]] Graphics::BindingGroupLayoutHandle MaterialLayoutOf(const ShaderVariant& variant);
        [[nodiscard]] Graphics::BindingGroupHandle OutputGroup(Graphics::TextureHandle sceneColour);
        /** @brief Adds the pass that clears the window to black, for a frame with no view into it. */
        static void AddWindowClear(RenderGraph& graph, const FrameContext& frame);
        void DestroyPipelinesOf(const ShaderEntry& shader);
        void DestroyMaterial(MaterialEntry& material);
        void DestroyMesh(MeshEntry& mesh);
        [[nodiscard]] std::vector<ResolvedDraw> ResolveDraws();
        /** @brief The draw set's group for frame `frameIndex`: its copies of the draw and palette buffers. */
        [[nodiscard]] Graphics::BindingGroupHandle DrawGroupFor(std::uint64_t frameIndex);
        [[nodiscard]] std::uint32_t NextId();

        Graphics::GraphicsDevice& m_device;
        ILogger& m_logger;
        /** @brief The scene targets' samples per pixel. */
        std::uint32_t m_samples;

        std::map<AssetKey, ShaderEntry> m_shaders;
        std::map<AssetKey, MeshEntry> m_meshes;
        std::map<AssetKey, MaterialEntry> m_materials;
        std::map<AssetKey, TargetEntry> m_targets;
        std::map<AssetKey, ImageEntry> m_images;
        std::map<AssetKey, CubemapEntry> m_cubemaps;
        /** @brief The samplers made for material parameters, by description. */
        std::vector<std::pair<SamplerDescription, Graphics::SamplerHandle>> m_samplers;
        std::map<PipelineKey, PipelineEntry> m_pipelines;
        std::uint32_t m_nextId = 1;

        /** @brief The fallback material: its program, laid out as any shader, and drawn with no material group. */
        ShaderEntry m_fallback;
        ShaderContainer m_background;
        ShaderContainer m_output;

        Graphics::BindingGroupLayoutHandle m_frameLayout;
        Graphics::BindingGroupLayoutHandle m_viewLayout;
        Graphics::BindingGroupLayoutHandle m_drawLayout;
        Graphics::BindingGroupLayoutHandle m_backgroundLayout;
        /** @brief Set 2 of the Background pass's CUBEMAP variant: the Sky cubemap. */
        Graphics::BindingGroupLayoutHandle m_skyLayout;
        /** @brief Set 2 of the Output pass: the view's SceneColor and its sampler. */
        Graphics::BindingGroupLayoutHandle m_outputLayout;
        /** @brief Set 1 of the Output pass: the Output block, the view's exposure and tonemapper. */
        Graphics::BindingGroupLayoutHandle m_outputBlockLayout;
        Graphics::PipelineHandle m_backgroundPipeline;
        Graphics::PipelineHandle m_skyPipeline;
        Graphics::PipelineHandle m_outputPipeline;

        /** @brief The default textures, by DefaultTexture: white, black and the flat normal. */
        std::array<Graphics::TextureHandle, 3> m_defaultTextures{};
        Graphics::SamplerHandle m_defaultSampler;
        Graphics::SamplerHandle m_outputSampler;
        Graphics::SamplerHandle m_skySampler;
        /** @brief The constant every vertex stream a mesh lacks reads, at stride 0. */
        Graphics::BufferHandle m_defaultStreams;

        std::unique_ptr<BlockRing> m_frameBlocks;
        std::unique_ptr<BlockRing> m_viewBlocks;
        std::unique_ptr<BlockRing> m_backgroundBlocks;
        std::unique_ptr<BlockRing> m_outputBlocks;
        std::unique_ptr<BlockRing> m_drawBlocks;
        /** @brief The palette buffer, one GpuBone a slot. */
        std::unique_ptr<BlockRing> m_paletteBlocks;
        /** @brief Each copy's draw set group, by frame index modulo the copies. */
        std::vector<DrawGroup> m_drawGroups;
        /** @brief The Output pass's binding group for each SceneColor texture, while the texture lives. */
        std::map<std::pair<std::uint32_t, std::uint32_t>, Graphics::BindingGroupHandle> m_outputGroups;

        SceneFrame m_frame;
        std::vector<DrawnView> m_drawnViews;
        SceneStats m_stats;
        /** @brief The missing vertex streams already logged, by mesh and vertex location. */
        std::set<std::pair<AssetKey, std::uint32_t>> m_loggedStreams;
        /** @brief The images views could not sample already logged, by the view's target and the image. */
        std::set<std::pair<AssetKey, AssetKey>> m_loggedSampling;
        /** @brief Shaders without `SKINNED` that skinned draws fell back from, already logged. */
        std::set<AssetKey> m_loggedUnskinned;
    };
} // namespace Fenrir::Render
