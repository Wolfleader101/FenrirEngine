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
#include "Core/Bloom.hpp"
#include "Core/Gtao.hpp"
#include "Core/RenderFeature.hpp"
#include "Core/SceneFrame.hpp"
#include "Core/SkyLighting.hpp"
#include "Device/GraphicsDevice.hpp"
#include "FenrirAssets/ImageData.hpp"
#include "FenrirAssets/InstanceListData.hpp"
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
        /**
         * @brief Draws of draw-list items in the last frame's Opaque and Transparent passes: one per merged run of
         * matching items (ADR-0051), and one per transparent item, per view.
         */
        std::uint32_t draws = 0;
        /**
         * @brief Draw-list items drawn in the last frame, one per item per view, merged or not, and one per copy of an
         * instance list.
         */
        std::uint32_t instances = 0;
        /** @brief Meshes, images, material blocks and instance lists uploaded since the last call. */
        std::uint32_t uploads = 0;
        /** @brief Bytes written into the last frame's palette buffer. */
        std::uint32_t paletteBytes = 0;
        /** @brief Skeleton palettes the last frame's palette buffer held (SceneFrame::palettes). */
        std::uint32_t palettes = 0;
        /** @brief Skinned items drawn in the last frame, one per item per view; part of `instances`. */
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
        /** @brief Its view-space normals (RG16F, octahedral), which its DepthPrepass pass writes (ADR-0049). */
        GraphResource sceneNormals;
        /**
         * @brief Its view block's binding group this frame: binding set 1, as `Fenrir/View.glsl` declares it, with the
         * white texture as its ambient occlusion.
         */
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
     * float, reversed-Z, cleared to 0) at its viewport's size with the configured MSAA samples, and its passes, after
     * the `Prepare` stage's `Panorama` passes for any panorama prepared since the last frame:
     * `DepthPrepass` (ADR-0049), one draw per `Opaque` or `Masked` item in Opaque's order, laying down SceneDepth and
     * the view-space normal of each pixel's nearest surface into `SceneNormals` (RG16F, octahedral as
     * `Fenrir/Normals.glsl` packs it, cleared to 0, at the same size and samples), through the item's own vertex stage
     * and `builtin:depth-prepass`'s fragment stage, whose ALPHA_MASK variant cuts a Masked item out by its material's
     * `baseColour` alpha, `baseColourTexture` and alpha cutoff, as builtin:lit does; `Opaque`, one draw per `Opaque` or
     * `Masked` item, testing depth for equality without writing it, so each pixel is shaded once (an item with no
     * prepass draw, a Masked material whose shader has neither `baseColour` nor `baseColourTexture` or a vertex stage
     * the prepass's fragment stage cannot follow, is drawn there testing and writing depth as before, and is missing
     * from SceneNormals); `Background` at depth 0, the view's colour or
     * its Sky cubemap or panorama's cube texture (sampled by the direction each pixel looks along, turned by the
     * sky's rotation and scaled by its intensity, from the frame block); `Transparent`, when there are `AlphaBlend` or
     * `Additive` items, depth tested without writing, drawn back to front by the view-space depth of each item's world
     * origin (no order-independent transparency, so intersecting or nested transparent items can sort wrongly); the
     * view's bloom in `PostProcess` (BloomChain), unless its intensity is 0; and `Output`, which resolves SceneColor,
     * scales it by the view's exposure and tonemaps it into its viewport of its render target, the window's backbuffer
     * or an image. The first view into a target clears it to black. With no view the backbuffer is cleared to black.
     *
     * **Ambient occlusion** (ADR-0049): a view whose camera has it gets Gtao's passes after its DepthPrepass pass, and
     * its `Opaque` pass binds their occlusion texture at set 1 binding 1 (`Fenrir/View.glsl`), which
     * `Fenrir/Lighting.glsl` darkens the sky's light by. Every other pass, and every view without it, binds the white
     * texture there. A view showing its occlusion (`ViewRequest::showOcclusion`) has Output draw that texture as grey
     * instead of SceneColor, so the passes only SceneColor needs are culled; it shows black without the passes.
     *
     * **Variants** (ADR-0024): the renderer picks the keys. A `Masked` material draws with its shader's `ALPHA_MASK`
     * variant if the shader has the key, its `MaterialData::alphaCutoff` written into the variant's `alphaCutoff` block
     * member. A skinned draw (ADR-0035) draws with the `SKINNED` variant; a shader without the key draws it with the
     * fallback material's, logged once per shader as a warning. Every other key stays off.
     *
     * **Skinning** (ADR-0035): each frame's palettes are written into the frame's palette buffer, set 3 binding 2, as
     * `GpuBone`s, ring-buffered per frame in flight and grown by doubling, with nothing kept across frames. A skinned
     * draw's record carries its `paletteOffset`; one whose palette range (`paletteOffset` plus its mesh's bone count)
     * passes the buffer's capacity is an internal error and is not drawn. Every record also carries the sign of its
     * world matrix's determinant, for the shaders' tangent handedness.
     *
     * **Compute skinning** (ADR-0050), on unless turned off: a `Skinning` pass, in its stage before any view's, runs
     * `builtin:skinning` once per skinned mesh and palette range drawn this frame, however many items, views and passes
     * draw it, into the frame's skinned vertex buffer (ring-buffered per frame in flight, grown by doubling): the
     * model-space positions, normals and tangents the `SKINNED` variant would make, for the skinned draws of the
     * built-in mesh programs (`builtin:lit`, `builtin:unlit` and the fallback). Those draws then use the variant
     * without `SKINNED`, reading those three streams from that buffer and the rest from the mesh, rigidly under the
     * skeleton entity's world matrix; they merge only with draws of the same skinned vertices. A user shader's skinned
     * draws keep its own `SKINNED` variant, a shader without the key still draws skinned meshes as the fallback
     * material (whose draws are compute-skinned), and with compute skinning off, or without the compute pipeline, every
     * skinned draw skins in its vertex stage as before.
     *
     * **Instance lists** (ADR-0051) are prepared like assets: each copy's transform, normal matrix and colour, kept
     * packed back to back with every other list's in one storage buffer, set 3 binding 1, made again whenever a list
     * is prepared or released, so a list that never changes is uploaded once. An item naming one is one draw of as many
     * instances as the list has copies, from its own record: `Fenrir/Draw.glsl` composes each copy's record from it.
     * It never merges with other items, and is never culled.
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
         * targets of `samples` samples per pixel (1: no MSAA). With `automaticInstancing`, consecutive matching items
         * draw as one instanced draw (ADR-0051).
         */
        SceneRenderer(Graphics::GraphicsDevice& device, ILogger& logger, std::uint32_t samples = 1,
                      bool automaticInstancing = true, bool computeSkinning = true);
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

        [[nodiscard]] bool IsPanoramaCurrent(AssetKey key, std::uint32_t revision) const;
        /**
         * @brief A panorama for a Sky (ADR-0047), a flat image with pixels twice as wide as high: uploaded with every
         * mip level, then drawn by the next AddPasses' `Panorama` passes into an RGBA16F cube texture with faces half
         * as wide as the image is high and every mip level, which the Background pass's CUBEMAP variant samples as it
         * does a Sky cubemap. The image's own texture is freed once the passes have run.
         */
        void PreparePanorama(AssetKey key, std::uint32_t revision, const ImageData& image);
        void ReleasePanorama(AssetKey key);

        [[nodiscard]] bool IsTargetCurrent(AssetKey key, std::uint32_t revision) const;
        /** @brief A render target image: an RGBA8 sRGB texture of `size` that views draw into and materials sample. */
        void PrepareTarget(AssetKey key, std::uint32_t revision, Graphics::PixelSize size);
        void ReleaseTarget(AssetKey key);

        [[nodiscard]] bool IsInstanceListCurrent(AssetKey key, std::uint32_t revision) const;
        /** @brief An instance list's copies, uploaded into the instance-list buffer before the next frame's draws. */
        void PrepareInstanceList(AssetKey key, std::uint32_t revision, const InstanceListData& list);
        void ReleaseInstanceList(AssetKey key);

        /** @brief The keys with GPU resources, for the facade to release those that went stale. */
        [[nodiscard]] std::vector<AssetKey> TargetKeys() const;
        [[nodiscard]] std::vector<AssetKey> ImageKeys() const;
        [[nodiscard]] std::vector<AssetKey> CubemapKeys() const;
        [[nodiscard]] std::vector<AssetKey> PanoramaKeys() const;
        [[nodiscard]] std::vector<AssetKey> ShaderKeys() const;
        [[nodiscard]] std::vector<AssetKey> MeshKeys() const;
        [[nodiscard]] std::vector<AssetKey> MaterialKeys() const;
        [[nodiscard]] std::vector<AssetKey> InstanceListKeys() const;
        /** @} */

        /** @brief Whether matching draws merge into instanced draws from the next frame (ADR-0051). */
        void SetAutomaticInstancing(bool on) noexcept
        {
            m_automaticInstancing = on;
        }

        /** @brief Whether skinned meshes are skinned in compute from the next frame (ADR-0050). */
        void SetComputeSkinning(bool on) noexcept
        {
            m_computeSkinning = on;
        }

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
            /**
             * @brief Whether its skinned draws are compute-skinned: a built-in mesh program's. A user shader's are
             * drawn with its own `SKINNED` variant, which may do more than skin (ADR-0050).
             */
            bool computeSkinned = false;
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
            Graphics::TextureDesc desc;
            Graphics::BindingGroupHandle group;
        };

        /**
         * @brief A prepared Sky panorama: its cube texture and the Background pass's group sampling it, and until its
         * `Panorama` passes have run, the image's own texture and those passes' blocks and groups.
         */
        struct PanoramaEntry
        {
            std::uint32_t revision = 0;
            Graphics::TextureHandle cube;
            /** @brief The cube texture's description: its faces' side and mip levels. */
            Graphics::TextureDesc cubeDesc;
            Graphics::BindingGroupHandle group;
            Graphics::TextureHandle source;
            /** @brief Each pass's block (face, half a texel, the panorama's mip), one uniform slot a pass. */
            Graphics::BufferHandle params;
            /** @brief Each pass's group, face by face within each mip level, from mip 0. */
            std::vector<Graphics::BindingGroupHandle> passGroups;
            /** @brief Whether its passes have been recorded, so its source can go. */
            bool converted = false;
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
            std::uint32_t vertexCount = 0;
            /** @brief A skinned mesh's vertex buffer as builtin:skinning reads it, its set 1; else empty. */
            Graphics::BindingGroupHandle skinSource;
        };

        /** @brief An instance list's copies in the instance-list buffer: its first record and how many; 0 for none. */
        struct InstanceRange
        {
            std::uint32_t first = 0;
            std::uint32_t count = 0;
        };

        /** @brief A prepared instance list: its copies' records back to back, and where they sit as last packed. */
        struct InstanceListEntry
        {
            std::uint32_t revision = 0;
            std::vector<std::byte> records;
            InstanceRange range;
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
            /** @brief Whether its items draw in the depth prepass: all but Masked ones it cannot cut out. */
            bool prepassed = true;
            /** @brief A Masked material's prepass set 2: its base colour alpha and cutoff, and its base colour texture.
             */
            Graphics::BufferHandle prepassBlock;
            std::vector<Graphics::BindingGroupEntry> prepassBindings;
            Graphics::BindingGroupHandle prepassGroup;
            /** @brief Which of `images` its base colour texture is, if any, and its group with that image replaced. */
            std::optional<std::size_t> prepassImage;
            Graphics::BindingGroupHandle prepassWithout;
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
            /** @brief Drawn over the depth prepass: depth tested for equality, not written. */
            bool prepassed = false;

            friend auto operator<=>(const PipelineKey&, const PipelineKey&) = default;
        };

        /**
         * @brief What a depth prepass pipeline is made for: the item's shader and variant, whose vertex stage it runs,
         * the streams it reads that the mesh has, whether it cuts a Masked item out, and culling.
         */
        struct PrepassKey
        {
            const ShaderEntry* shader = nullptr;
            const ShaderVariant* variant = nullptr;
            std::uint32_t streams = 0;
            bool masked = false;
            bool doubleSided = false;

            friend auto operator<=>(const PrepassKey&, const PrepassKey&) = default;
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

        /** @brief A made depth prepass pipeline, and the variant it was made from: the item's vertex stage, then ours.
         */
        struct PrepassEntry
        {
            PipelineEntry pipeline;
            std::unique_ptr<ShaderVariant> variant;
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
            /** @brief Its depth prepass pipeline; null when it has none, so Opaque writes its depth. */
            const PipelineEntry* prepass = nullptr;
            /** @brief Cut out in the prepass, which binds its material's prepass group at set 2. */
            bool prepassMasked = false;
            /** @brief An instance list's copies; none for one item. */
            InstanceRange instances;
            /**
             * @brief A compute-skinned draw's skinned vertices in the frame's skinned vertex buffer: the byte offset of
             * its positions, followed by its normals and its tangents. Empty for every other draw.
             */
            std::optional<std::size_t> skinnedVertices;
        };

        /** @brief One dispatch of the frame's Skinning pass: a skinned mesh and palette range, and where they go. */
        struct SkinDispatch
        {
            const MeshEntry* mesh = nullptr;
            std::uint32_t paletteOffset = 0;
            /** @brief The byte offset of its skinned vertices, as ResolvedDraw::skinnedVertices. */
            std::size_t skinnedVertices = 0;
        };

        /** @brief One frame in flight's set 2 of builtin:skinning: the palette and skinned vertex buffers it binds. */
        struct SkinTargetGroup
        {
            Graphics::BufferHandle palette;
            Graphics::BufferHandle skinned;
            Graphics::BindingGroupHandle group;
        };

        /**
         * @brief One copy's frame set binding group (set 0), for the frame block, light buffer and sky textures it was
         * made with.
         */
        struct FrameGroup
        {
            Graphics::BufferHandle block;
            Graphics::BufferHandle lights;
            Graphics::TextureHandle irradiance;
            Graphics::TextureHandle specular;
            Graphics::BindingGroupHandle group;
        };

        /**
         * @brief One frame in flight's draw set binding group (set 3), for the draw, palette and instance-list
         * buffers it was made with.
         */
        struct DrawGroup
        {
            Graphics::BufferHandle draws;
            Graphics::BufferHandle palette;
            Graphics::BufferHandle instanceLists;
            Graphics::BindingGroupHandle group;
        };

        /** @brief The pipeline drawing a mesh with the standard streams `streams` (StreamsOf's bits) has. */
        [[nodiscard]] const PipelineEntry& MeshPipeline(const ShaderEntry& shader, const VariantEntry& variant,
                                                        std::uint32_t streams, BlendMode blend, bool doubleSided,
                                                        bool prepassed);
        /** @brief The depth prepass pipeline running `variant`'s vertex stage; why it failed is logged as a warning. */
        [[nodiscard]] const PipelineEntry& PrepassPipeline(const ShaderEntry& shader, const VariantEntry& variant,
                                                           std::uint32_t streams, bool masked, bool doubleSided);
        /**
         * @brief Makes a Masked material's prepass block and group from its variant, data and block as written, or
         * marks it as drawn without a prepass when its shader has no base colour to cut it out by.
         */
        void PrepareMaskedPrepass(MaterialEntry& entry, const ShaderVariant& variant, const MaterialData& material,
                                  std::span<const std::byte> block);
        /** @brief A Masked material's prepass group, with its base colour texture replaced if it is in `replaced`. */
        [[nodiscard]] Graphics::BindingGroupHandle PrepassGroup(MaterialEntry& material,
                                                                const std::vector<AssetKey>& replaced);
        /**
         * @brief Consecutive draws of a pass, one instance each: `first` is the record of the first, and the rest
         * follow it in the draw buffer.
         */
        struct DrawRun
        {
            std::size_t first = 0;
            std::uint32_t count = 1;
        };

        /**
         * @brief `order`'s opaque or masked draws as the runs a pass draws (ADR-0051): with automatic instancing on,
         * each run of consecutive records with the same pipeline (the prepass's when `prepass`), material, set 2 group
         * and mesh is one instanced draw; with it off, each draw is its own. An instance list's draw is always its own.
         */
        [[nodiscard]] std::vector<DrawRun> RunsOf(const std::vector<ResolvedDraw>& draws,
                                                  const std::vector<Graphics::BindingGroupHandle>& groups,
                                                  const std::vector<std::size_t>& order, bool prepass) const;
        /**
         * @brief Records `runs` of `draws` into a pass, binding each one's pipeline (its prepass pipeline when
         * `prepass`), groups and streams, a compute-skinned draw's first three from `skinned`, and drawing each run's
         * instances.
         */
        void RecordDraws(Graphics::PassEncoder& encoder, const std::vector<ResolvedDraw>& draws,
                         const std::vector<Graphics::BindingGroupHandle>& groups, const std::vector<DrawRun>& runs,
                         Graphics::BindingGroupHandle frameGroup, Graphics::BindingGroupHandle viewGroup,
                         Graphics::BindingGroupHandle drawGroup, Graphics::BufferHandle skinned,
                         Graphics::PixelRect viewport, bool prepass) const;
        [[nodiscard]] Graphics::PipelineHandle MakeInternalPipeline(
            const ShaderContainer& container, const ShaderVariant* variant, Graphics::BindingGroupLayoutHandle layout,
            Graphics::TextureFormat colour, bool depthTested, std::uint32_t samples,
            Graphics::BindingGroupLayoutHandle setOneLayout = {},
            Graphics::BindingGroupLayoutHandle setZeroLayout = {});
        /** @brief Adds the `Panorama` passes of each panorama not yet drawn into its cube texture. */
        void AddPanoramaPasses(RenderGraph& graph);
        /** @brief Frees a panorama's image texture and its passes' blocks and groups, if it still has them. */
        void FreePanoramaPasses(PanoramaEntry& entry);
        /** @brief The Background pass's group for the Sky cubemap or panorama `key`; empty if neither is prepared. */
        [[nodiscard]] Graphics::BindingGroupHandle SkyGroup(AssetKey key) const;
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
        /** @brief Packs every instance list into a new instance-list buffer, if one was prepared or released since. */
        void PackInstanceLists();
        /**
         * @brief The frame's draws, sorted; with compute skinning, each skinned mesh and palette range they draw is one
         * of `skins`, whose skinned vertices its draws read.
         */
        [[nodiscard]] std::vector<ResolvedDraw> ResolveDraws(std::vector<SkinDispatch>& skins);
        /**
         * @brief Adds the Skinning pass running `skins` into the frame's skinned vertex buffer, imported into `graph`
         * as the returned resource; empty, with no pass, when there are none.
         */
        [[nodiscard]] GraphResource AddSkinningPass(RenderGraph& graph, std::uint64_t frameIndex,
                                                    const std::vector<SkinDispatch>& skins);
        /** @brief The frame set's group for frame `frameIndex`: its copy of the frame block, and the sky's light. */
        [[nodiscard]] Graphics::BindingGroupHandle FrameGroupFor(std::uint64_t frameIndex);
        /** @brief What the scene's Sky lights with: its prepared cube texture, else its colour. */
        [[nodiscard]] SkySource SkySourceOf(const SceneLighting& lighting) const;
        /**
         * @brief The view set's group for slot `slot` of frame `frameIndex`'s view blocks, with `occlusion` as its
         * ambient occlusion, made once while both live.
         */
        [[nodiscard]] Graphics::BindingGroupHandle ViewGroupFor(std::uint64_t frameIndex, std::size_t slot,
                                                                Graphics::TextureHandle occlusion);
        /**
         * @brief The draw set's group for frame `frameIndex`: its copies of the draw and palette buffers, and the
         * instance-list buffer.
         */
        [[nodiscard]] Graphics::BindingGroupHandle DrawGroupFor(std::uint64_t frameIndex);
        [[nodiscard]] std::uint32_t NextId();

        Graphics::GraphicsDevice& m_device;
        ILogger& m_logger;
        /** @brief The scene targets' samples per pixel. */
        std::uint32_t m_samples;
        /** @brief RendererConfig::automaticInstancing: whether RunsOf merges matching draws. */
        bool m_automaticInstancing;
        /** @brief RendererConfig::computeSkinning: whether skinned draws read the Skinning pass's vertices. */
        bool m_computeSkinning;

        std::map<AssetKey, ShaderEntry> m_shaders;
        std::map<AssetKey, MeshEntry> m_meshes;
        std::map<AssetKey, MaterialEntry> m_materials;
        std::map<AssetKey, TargetEntry> m_targets;
        std::map<AssetKey, ImageEntry> m_images;
        std::map<AssetKey, CubemapEntry> m_cubemaps;
        std::map<AssetKey, PanoramaEntry> m_panoramas;
        std::map<AssetKey, InstanceListEntry> m_instanceLists;
        /**
         * @brief Every instance list's copies, back to back (`Fenrir/Draw.glsl`'s FenrirInstance), at set 3 binding 1:
         * one zeroed record when there are none.
         */
        Graphics::BufferHandle m_instanceListBuffer;
        /** @brief Whether a list was prepared or released since the instance-list buffer was packed. */
        bool m_instancesChanged = true;
        /** @brief The samplers made for material parameters, by description. */
        std::vector<std::pair<SamplerDescription, Graphics::SamplerHandle>> m_samplers;
        std::map<PipelineKey, PipelineEntry> m_pipelines;
        std::map<PrepassKey, PrepassEntry> m_prepassPipelines;
        std::uint32_t m_nextId = 1;

        /** @brief The fallback material: its program, laid out as any shader, and drawn with no material group. */
        ShaderEntry m_fallback;
        ShaderContainer m_background;
        ShaderContainer m_output;
        ShaderContainer m_panorama;
        /** @brief `builtin:depth-prepass`, whose fragment stages the prepass pipelines run. */
        ShaderContainer m_prepass;
        /** @brief `builtin:skinning`, the Skinning pass's compute program, and its pipeline; empty if it failed. */
        ShaderContainer m_skinning;
        Graphics::PipelineHandle m_skinningPipeline;
        /** @brief Its sets: 0 a dispatch's job block, 1 the mesh's vertices, 2 the palette and skinned vertices. */
        Graphics::BindingGroupLayoutHandle m_skinJobLayout;
        Graphics::BindingGroupLayoutHandle m_skinSourceLayout;
        Graphics::BindingGroupLayoutHandle m_skinTargetLayout;

        Graphics::BindingGroupLayoutHandle m_frameLayout;
        Graphics::BindingGroupLayoutHandle m_viewLayout;
        Graphics::BindingGroupLayoutHandle m_drawLayout;
        Graphics::BindingGroupLayoutHandle m_backgroundLayout;
        /** @brief Set 2 of the Background pass's CUBEMAP variant: the Sky cubemap. */
        Graphics::BindingGroupLayoutHandle m_skyLayout;
        /** @brief Set 2 of the Panorama passes: their block and the panorama. */
        Graphics::BindingGroupLayoutHandle m_panoramaLayout;
        /** @brief Set 2 of a Masked item's prepass pipeline: its block (alpha, cutoff) and base colour texture. */
        Graphics::BindingGroupLayoutHandle m_prepassMaskLayout;
        /** @brief Set 2 of the Output pass: the view's SceneColor and its sampler. */
        Graphics::BindingGroupLayoutHandle m_outputLayout;
        /** @brief Set 1 of the Output pass: the Output block, the view's exposure and tonemapper. */
        Graphics::BindingGroupLayoutHandle m_outputBlockLayout;
        Graphics::PipelineHandle m_backgroundPipeline;
        Graphics::PipelineHandle m_skyPipeline;
        Graphics::PipelineHandle m_outputPipeline;
        /** @brief Output's OCCLUSION variant, which draws a view's ambient occlusion as grey. */
        Graphics::PipelineHandle m_occlusionOutputPipeline;
        Graphics::PipelineHandle m_panoramaPipeline;

        /** @brief The default textures, by DefaultTexture: white, black and the flat normal. */
        std::array<Graphics::TextureHandle, 3> m_defaultTextures{};
        /** @brief The rect lights' LTC tables (ADR-0048): the matrix's at set 0 binding 5, the amplitude's at 6. */
        Graphics::TextureHandle m_ltcMatrix;
        Graphics::TextureHandle m_ltcAmplitude;
        Graphics::SamplerHandle m_defaultSampler;
        Graphics::SamplerHandle m_outputSampler;
        Graphics::SamplerHandle m_skySampler;
        /** @brief Wraps round a panorama's sides and clamps at its top and bottom. */
        Graphics::SamplerHandle m_panoramaSampler;
        /** @brief The constant every vertex stream a mesh lacks reads, at stride 0. */
        Graphics::BufferHandle m_defaultStreams;

        std::unique_ptr<BlockRing> m_frameBlocks;
        /** @brief The point, spot and rect lights (ADR-0048), bound at set 0 binding 4 by the frame's groups. */
        std::unique_ptr<BlockRing> m_lightBlocks;
        std::unique_ptr<BlockRing> m_viewBlocks;
        std::unique_ptr<BlockRing> m_backgroundBlocks;
        std::unique_ptr<BlockRing> m_outputBlocks;
        std::unique_ptr<BlockRing> m_drawBlocks;
        /** @brief The palette buffer, one GpuBone a slot. */
        std::unique_ptr<BlockRing> m_paletteBlocks;
        /** @brief Each Skinning dispatch's job block, one uniform slot a dispatch, bound at its set 0. */
        std::unique_ptr<BlockRing> m_skinJobs;
        /** @brief The skinned vertex buffer, one copy per frame in flight, which the Skinning pass writes. */
        std::unique_ptr<FrameRingBuffer> m_skinnedVertices;
        /** @brief Its size in bytes. */
        std::size_t m_skinnedCapacity = 0;
        /** @brief builtin:skinning's set 2 per copy, by frame index modulo the copies. */
        std::vector<SkinTargetGroup> m_skinTargetGroups;
        /** @brief The sky filtered into image-based lighting, which set 0 binds. */
        std::unique_ptr<SkyLighting> m_skyLighting;
        /** @brief Each view's bloom passes, in `PostProcess`. */
        std::unique_ptr<BloomChain> m_bloom;
        /** @brief The GTAO passes of each view that has ambient occlusion, after its prepass. */
        std::unique_ptr<Gtao> m_gtao;
        /** @brief Each copy's frame set group, by frame index modulo the copies. */
        std::vector<FrameGroup> m_frameGroups;
        /** @brief Each copy's draw set group, by frame index modulo the copies. */
        std::vector<DrawGroup> m_drawGroups;
        /**
         * @brief The view set's groups, by the view ring's buffer (index, generation), slot and the occlusion texture
         * (index, generation), while both live.
         */
        std::map<std::array<std::uint32_t, 5>, Graphics::BindingGroupHandle> m_viewGroups;
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
