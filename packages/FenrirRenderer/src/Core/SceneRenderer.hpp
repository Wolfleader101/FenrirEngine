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
#include <string_view>
#include <utility>
#include <vector>

#include "Core/BindingGroupCache.hpp"
#include "Core/BlockRing.hpp"
#include "Core/Bloom.hpp"
#include "Core/Cascades.hpp"
#include "Core/Gtao.hpp"
#include "Core/LightClusters.hpp"
#include "Core/PointShadows.hpp"
#include "Core/ReflectionTrace.hpp"
#include "Core/RenderFeature.hpp"
#include "Core/SceneFrame.hpp"
#include "Core/SceneHistory.hpp"
#include "Core/ShadowPool.hpp"
#include "Core/SkyImages.hpp"
#include "Core/SkyLighting.hpp"
#include "Core/SpotShadows.hpp"
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
        /**
         * @brief Draws in the last frame's shadow passes (ADR-0050): one per merged run of casting items, per cascade
         * of each view, per shadowed spot light, and per shadowed point light (six when its faces are drawn one at a
         * time).
         */
        std::uint32_t shadowDraws = 0;
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
        /** @brief Its sun's shadow atlas (ADR-0050), which its ShadowCascades pass writes; empty without one. */
        GraphResource shadowAtlas;
        /**
         * @brief Its view block's binding group this frame: binding set 1, as `Fenrir/View.glsl` declares it, with the
         * white texture as its ambient occlusion and no shadow.
         */
        Graphics::BindingGroupHandle viewGroup;
        /**
         * @brief Its cull pass's command buffer (ADR-0057), the indirect commands its DepthPrepass and Opaque passes
         * draw from, one per run; empty when it has no opaque runs.
         */
        Graphics::BufferHandle cullCommands;
        /** @brief Its cull pass's indirection, which those draws' instances read; empty when it has no opaque runs. */
        Graphics::BufferHandle cullIndirection;
        /** @brief How many runs its cull pass counts into. */
        std::uint32_t cullRuns = 0;
        /** @brief How many candidates its cull pass culls. */
        std::uint32_t cullCandidates = 0;
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
     * **The sun's shadow** (ADR-0050): with SceneLighting::shadows on and items that cast, each view gets FitCascades'
     * cascades out to the shadow distance and a `ShadowCascades` pass, in the `Shadow` stage, drawing every casting
     * `Opaque` and `Masked` item into each cascade's tile of a transient Depth32Float atlas (the cascades side by side,
     * each `mapSize` square, reversed-Z, cleared to 0), through the item's own vertex stage with the cascade's light
     * matrix as its view block, and `builtin:shadow`'s fragment stage, whose ALPHA_MASK variant cuts a Masked item out
     * from its prepass mask group (one whose shader has no base colour casts whole); culling is off, and matching items
     * merge as they do in Opaque. Opaque and Transparent bind the atlas at set 1 binding 2 with a comparison sampler,
     * and the cascades in the view block (`Fenrir/View.glsl`), which `Fenrir/Shadows.glsl` reads; every other pass, and
     * every view without them, binds a 1 x 1 map that lights everything, with no cascades. A view showing its cascades
     * tints its lit colour by them; one showing its shadow atlas has Output draw the atlas as grey.
     *
     * **Spot shadows** (ADR-0050): each frame the spot lights with castShadows nearest to any view's camera, up to
     * the budget, keep their shadows (the spot shadow pool), the rest light unshadowed, warned about once. With items
     * that cast, one `SpotShadows` pass in the `Shadow` stage, before every view's, draws them as ShadowCascades does
     * into each light's tile of the spot shadow atlas: a Depth32Float texture the renderer keeps, a grid of the
     * budget's tiles (SpotAtlasGridOf), each `mapSize` square, made again when the budget changes, through a view
     * block with the light's perspective (FitSpotShadow). Materials sampling render target images sample their
     * defaults there, so the pass waits on no view. The light buffer holds those lights first, frame block
     * `lights.y` counting them, and set 0 binds their records (GpuSpotShadow) at binding 7 and the atlas at binding 8
     * (`Fenrir/Frame.glsl`), which `Fenrir/Shadows.glsl` reads; with none, and in the SpotShadows pass itself,
     * binding 8 is the map that lights everything. A view showing the spot shadow atlas has Output draw it as grey.
     *
     * **Point shadows** (ADR-0050): chosen as spot shadows are, within their own budget (the point shadow pool), each
     * shadowed point light a cube of the point shadow cubes (CubeArray, Depth32Float, `mapSize` square faces, a
     * cube per budget, kept and made again when the budget changes). Casters are drawn from the light's position
     * through six 90-degree faces (FitPointShadow) behind builtin:shadow's POINT_SHADOW variant, which writes the
     * distance from the light over its range. Where the device lets a vertex stage write gl_Layer, one `PointShadows`
     * pass attaches every layer and draws each light's casters once, as six instances each, through the item's own
     * POINT_SHADOW variant (`Fenrir/Draw.glsl`); casters whose shader has no POINT_SHADOW variant, and every caster on
     * a device without it, are drawn into each face by `PointShadowFaces` passes, one a face, through a view block of
     * that face's own. The light buffer holds those lights after the shadowed spot lights, frame block `lights.z`
     * counting them, and set 0 binds their records (GpuPointShadow) at binding 9 and the point shadow cubes at binding
     * 10; with none, and in the passes drawing it, binding 10 is a cube that lights everything.
     *
     * **Light clusters** (ADR-0049), on unless turned off: each view with point, spot or rect lights gets a
     * `LightClusters` compute pass, in the `DepthPrepass` stage, running `builtin:light-clusters` over its 16 x 9 x 24
     * clusters (Core/LightClusters): each light's sphere listed in every cluster its range touches, up to
     * MaxClusterLights a cluster, or the light count when fewer, into the view's own cluster buffer (one per view and
     * frame in flight, grown as the room needs). Its slices run exponentially from the near
     * plane to the farthest any light reaches. Set 1 binds that buffer at binding 3 and the view block says so
     * (`clusterInfo.x`), and `Fenrir/Lighting.glsl` loops over its pixel's cluster; every other view block, and every
     * view when clustering is off, binds a buffer of one 0 and loops over the whole light buffer.
     *
     * **Ambient occlusion** (ADR-0049): a view whose camera has it gets Gtao's passes after its DepthPrepass pass, and
     * its `Opaque` pass binds their occlusion texture at set 1 binding 1 (`Fenrir/View.glsl`), which
     * `Fenrir/Lighting.glsl` darkens the sky's light by. Every other pass, and every view without it, binds the white
     * texture there. A view showing its occlusion (`ViewRequest::showOcclusion`) has Output draw that texture as grey
     * instead of SceneColor, so the passes only SceneColor needs are culled; it shows black without the passes.
     *
     * **Reflection inputs** (ADR-0056): a view whose camera has reflections keeps, under its camera's entity id, a
     * history colour and two Hi-Z pyramids across frames (SceneHistory): its HiZ passes after GTAO's draw this frame's
     * pyramid from SceneDepth, and its SceneHistory passes, the first of `PostProcess`, copy SceneColor before bloom.
     * They write textures kept outside the graph, so they are never culled, and keep the passes SceneColor needs even
     * when Output shows grey. A camera with no such view in a frame has its textures freed.
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
     * **GPU culling** (ADR-0057): every view's opaque and masked runs (merged items, instance lists, and lone items as
     * runs of one) are candidates of a `Cull` compute pass per view, first in the frame, running `builtin:cull`: the
     * renderer writes each run's command template (its mesh's index count, no instances, and the base instance where
     * the run's region of the view's indirection starts) and group table entries (one per 64 candidates, naming the
     * run) into the frame's rings; the pass copies the templates over the view's command buffer and one dispatch
     * appends each candidate it keeps, `(record, copy)`, to its run's region, counting it into the command with an
     * atomic. `DepthPrepass` and `Opaque` then draw each run with one indirect draw of its command, declaring
     * `IndirectRead` on the commands and `StorageRead` on the indirection, which set 3 binding 3 binds for them
     * (`Fenrir/Draw.glsl`'s `CurrentDraw()` reads it), so there is one draw group per view and frame in flight; every
     * other pass binds the buffer that says there is no indirection and draws directly, as before. The per-view
     * buffers are sized to the frame's runs and candidates, kept between frames and ring-buffered per frame in flight.
     * The job block carries the view's planes (Math::Frustum), and the pass keeps a candidate whose sphere and then
     * whose box's eight corners may be inside them, as Math::Frustum tests them; with GPU culling off it gets no
     * planes and keeps every candidate. Before the runs form, a draw whose world bounds are outside the view's frustum
     * is left out of the view altogether, opaque or transparent.
     *
     * **Instance lists** (ADR-0051) are prepared like assets: each copy's transform, normal matrix and colour, kept
     * packed back to back with every other list's in one storage buffer, set 3 binding 1, made again whenever a list
     * is prepared or released, so a list that never changes is uploaded once. An item naming one is one draw of as many
     * instances as the list has copies, from its own record: `Fenrir/Draw.glsl` composes each copy's record from it.
     * It never merges with other items. Its bounds, the box around every copy's mesh box, are computed when it is
     * prepared (ADR-0057), and a transparent list outside a view's frustum is left out of that view as an item is.
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
         * draw as one instanced draw (ADR-0051); with `computeSkinning`, skinned meshes skin once a frame in compute;
         * `cascades` says how each view's sun cascades are made (ADR-0050), its count held to 1 to MaxCascades;
         * `spotShadows` the spot shadow budget and the side of each light's tile; `pointShadows` the point shadow
         * budget and the side of each cube face.
         */
        SceneRenderer(Graphics::GraphicsDevice& device, ILogger& logger, std::uint32_t samples = 1,
                      bool automaticInstancing = true, bool computeSkinning = true, CascadeConfig cascades = {},
                      ShadowBudgetConfig spotShadows = DefaultSpotShadows,
                      ShadowBudgetConfig pointShadows = DefaultPointShadows);
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
        /**
         * @brief An instance list's copies, uploaded into the instance-list buffer before the next frame's draws, and
         * its bounds, the box around every copy's `meshBounds` (the box of the mesh it draws), computed now.
         */
        void PrepareInstanceList(AssetKey key, std::uint32_t revision, const InstanceListData& list,
                                 const Math::AABB& meshBounds);
        void ReleaseInstanceList(AssetKey key);
        /**
         * @brief The box around every copy of a prepared instance list's mesh box `meshBounds`, in the drawing
         * entity's local space (ADR-0057): as computed when the list was uploaded, or again now for a mesh box other
         * than the one it was uploaded with. Empty for a list not prepared or with no copies.
         */
        [[nodiscard]] std::optional<Math::AABB> InstanceListBounds(AssetKey key, const Math::AABB& meshBounds);

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

        /**
         * @brief Whether each view's lights are clustered from the next frame (ADR-0049); off, every pixel loops over
         * the whole light buffer.
         */
        void SetClusteredLights(bool on) noexcept
        {
            m_clusteredLights = on;
        }

        /**
         * @brief Whether each view's cull pass tests its candidates against the view's planes from the next frame
         * (ADR-0057); off, it keeps every candidate, and only the coarse cull by entity on the CPU culls.
         */
        void SetGpuCulling(bool on) noexcept
        {
            m_gpuCulling = on;
        }

        /**
         * @brief How many spot lights may cast shadows from the next frame (ADR-0050); the spot shadow atlas is made
         * again at its new size when next needed.
         */
        void SetSpotShadowBudget(std::uint32_t budget);

        /**
         * @brief How many point lights may cast shadows from the next frame (ADR-0050); the point shadow cubes is
         * made again at its new size when next needed.
         */
        void SetPointShadowBudget(std::uint32_t budget);

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
            /** @brief What the texture was made with, which a pass that reads it imports it into the graph by. */
            Graphics::TextureDesc desc;
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
            /** @brief The mesh's local bounds (ADR-0021), which its draws' records carry (ADR-0057). */
            Math::AABB bounds{Math::Vec3(0.0f), Math::Vec3(0.0f)};
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
            /** @brief The mesh box `bounds` was computed with. */
            Math::AABB meshBounds{Math::Vec3(0.0f), Math::Vec3(0.0f)};
            /** @brief The box around every copy's mesh box, in the drawing entity's local space (ADR-0057). */
            Math::AABB bounds{Math::Vec3(0.0f), Math::Vec3(0.0f)};
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

        /**
         * @brief What a shadow pipeline is made for: the item's shader and variant, whose vertex stage it runs, the
         * streams it reads that the mesh has, whether it cuts a Masked item out, and whether it writes a point light's
         * distance (builtin:shadow's POINT_SHADOW variant).
         */
        struct ShadowKey
        {
            const ShaderEntry* shader = nullptr;
            const ShaderVariant* variant = nullptr;
            std::uint32_t streams = 0;
            bool masked = false;
            bool point = false;

            friend auto operator<=>(const ShadowKey&, const ShadowKey&) = default;
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
            /** @brief Its shadow pipeline (ADR-0050); null when it casts no shadow. */
            const PipelineEntry* shadow = nullptr;
            /** @brief Cut out in the shadow passes, which bind its material's prepass group at set 2 too. */
            bool shadowMasked = false;
            /** @brief Its pipeline for one face of a point light's cube at a time; null when it casts no shadow. */
            const PipelineEntry* pointShadow = nullptr;
            /**
             * @brief Its pipeline for all six faces as instances, through its shader's POINT_SHADOW variant; null when
             * the device or the shader cannot, so it is drawn a face at a time.
             */
            const PipelineEntry* pointShadowLayered = nullptr;
            /** @brief An instance list's copies; none for one item. */
            InstanceRange instances;
            /**
             * @brief What its record carries (ADR-0057): an item's world bounds, or an instance list's mesh's local
             * box.
             */
            Math::AABB recordBounds{Math::Vec3(0.0f), Math::Vec3(0.0f)};
            /** @brief The box it is culled by: `recordBounds` for an item, the box around every copy for a list. */
            Math::AABB worldBounds{Math::Vec3(0.0f), Math::Vec3(0.0f)};
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
         * @brief One copy's frame set binding groups (set 0), for the frame block, light buffer, sky textures, spot
         * shadow records and spot shadow atlas they were made with.
         */
        struct FrameGroup
        {
            Graphics::BufferHandle block;
            Graphics::BufferHandle lights;
            Graphics::TextureHandle irradiance;
            Graphics::TextureHandle specular;
            /** @brief The spot shadow records' buffer `group` binds at binding 7. */
            Graphics::BufferHandle spotShadows;
            /** @brief The spot shadow atlas `group` binds; empty for none. */
            Graphics::TextureHandle spotAtlas;
            /** @brief The point shadow records' buffer `group` binds at binding 9. */
            Graphics::BufferHandle pointShadows;
            /** @brief The point shadow cubes `group` binds; empty for none. */
            Graphics::TextureHandle pointShadowCubes;
            Graphics::BindingGroupHandle group;
            /**
             * @brief The same, but binding the maps that light everything for the atlas and the point shadow cubes, for
             * the shadow passes that draw them; `group` itself when it binds neither.
             */
            Graphics::BindingGroupHandle unshadowed;
        };

        /**
         * @brief One entry of the frame's group table, as `builtin:cull` declares it (std430, a uvec4): a work group's
         * run (ADR-0057) and which of its candidates the group culls.
         */
        struct CullGroupEntry
        {
            /** @brief The run's command in the view's command buffer. */
            std::uint32_t command = 0;
            /** @brief The run's first record: an instance list's one record, or the first of a merged run's. */
            std::uint32_t record = 0;
            /** @brief How many candidates the run has: a list's copies, or the merged run's items. */
            std::uint32_t count = 0;
            /** @brief The first candidate this group culls, within the run. */
            std::uint32_t first = 0;
        };
        static_assert(sizeof(CullGroupEntry) == 16, "a group table entry is a uvec4");

        /** @brief One view's cull pass this frame (ADR-0057): what it writes, and what its draws then read. */
        struct CullView
        {
            /** @brief The view's command buffer; empty when the view has no opaque runs. */
            Graphics::BufferHandle commands;
            /** @brief The view's indirection; empty when the view has no opaque runs. */
            Graphics::BufferHandle indirection;
            /** @brief The two, imported into the frame's graph. */
            GraphResource commandsResource;
            GraphResource indirectionResource;
            /** @brief Its templates' byte offset in the frame's template ring. */
            std::size_t templateOffset = 0;
            /** @brief Its templates' size in bytes. */
            std::size_t templateBytes = 0;
            /** @brief Its first entry in the frame's group table. */
            std::uint32_t firstGroup = 0;
            /** @brief How many group table entries it has: one dispatch of as many groups. */
            std::uint32_t groups = 0;
            /** @brief How many runs it counts into. */
            std::uint32_t runs = 0;
            /** @brief How many candidates it culls. */
            std::uint32_t candidates = 0;
            /** @brief The view's planes, which the pass tests each candidate against (ADR-0057). */
            Math::Frustum frustum;
        };

        /** @brief A buffer ringed per frame in flight and grown by doubling when a frame needs more than it holds. */
        struct GrownRing
        {
            std::unique_ptr<FrameRingBuffer> ring;
            /** @brief Each copy's size in bytes. */
            std::size_t capacity = 0;

            /** @brief Makes the ring hold at least `bytes`, remaking its copies with `usage` when it does not. */
            void Reserve(Graphics::GraphicsDevice& device, std::size_t bytes, Graphics::BufferUsage usage,
                         std::string_view label);
        };

        /** @brief A view's cull rings: its command buffer and its indirection. */
        struct CullRings
        {
            GrownRing commands;
            GrownRing indirection;
        };

        /** @brief The pipeline drawing a mesh with the standard streams `streams` (StreamsOf's bits) has. */
        [[nodiscard]] const PipelineEntry& MeshPipeline(const ShaderEntry& shader, const VariantEntry& variant,
                                                        std::uint32_t streams, BlendMode blend, bool doubleSided,
                                                        bool prepassed);
        /**
         * @brief The shadow pipeline running `variant`'s vertex stage behind builtin:shadow's fragment stage; why it
         * failed is logged as a warning, and the item casts no shadow.
         */
        [[nodiscard]] const PipelineEntry& ShadowPipeline(const ShaderEntry& shader, const VariantEntry& variant,
                                                          std::uint32_t streams, bool masked, bool point = false);
        /**
         * @brief The pipeline drawing a point light's six faces as instances: `variant`'s POINT_SHADOW variant's vertex
         * stage behind builtin:shadow's POINT_SHADOW fragment stage. Null, with nothing logged, when the device cannot
         * write gl_Layer from a vertex stage or `shader` has no such variant; why it otherwise failed is logged.
         */
        [[nodiscard]] const PipelineEntry* LayeredShadowPipeline(const ShaderEntry& shader, const VariantEntry& variant,
                                                                 std::uint32_t streams, bool masked);
        /**
         * @brief Makes `made`'s pipeline: `variant`'s vertex stage behind `program`'s fragment stage (its ALPHA_MASK
         * variant when `masked`, binding the prepass mask group at set 2, and its POINT_SHADOW one when `point`), with
         * `desc`'s render state and the streams `streams` the mesh has; why it could not, in its failure.
         */
        void MakeBehindVertexStage(PrepassEntry& made, const VariantEntry& variant, std::uint32_t streams,
                                   const ShaderContainer& program, bool masked, Graphics::PipelineDesc desc,
                                   std::string_view label, bool point = false);
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
        /** @brief Which of a draw's pipelines a pass draws it with. */
        enum class DrawPass : std::uint8_t
        {
            /** @brief Opaque's or Transparent's. */
            Main,
            Prepass,
            Shadow,
            /** @brief One face of a point light's cube. */
            PointShadow,
            /** @brief All six faces of a point light's cube, as six instances an item. */
            PointShadowLayered,
        };

        /** @brief The pipeline `pass` draws `draw` with: null when it has none there. */
        [[nodiscard]] static const PipelineEntry* PipelineOf(const ResolvedDraw& draw, DrawPass pass) noexcept;

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
         * each run of consecutive records with the same pipeline (`pass`'s), material, set 2 group and mesh is one
         * instanced draw; with it off, each draw is its own. An instance list's draw is always its own.
         */
        [[nodiscard]] std::vector<DrawRun> RunsOf(const std::vector<ResolvedDraw>& draws,
                                                  const std::vector<Graphics::BindingGroupHandle>& groups,
                                                  const std::vector<std::size_t>& order, DrawPass pass) const;
        /**
         * @brief Records `runs` of `draws` into a pass, binding each one's pipeline (`pass`'s), groups and streams, a
         * compute-skinned draw's first three from `skinned`, and drawing each run's instances: directly, or, with
         * `commands`, the view's command buffer a cull pass counted into (ADR-0057), as one indirect draw of the
         * command at the run's index. A run with no pipeline for `pass` is skipped, its command left in place.
         */
        void RecordDraws(Graphics::PassEncoder& encoder, const std::vector<ResolvedDraw>& draws,
                         const std::vector<Graphics::BindingGroupHandle>& groups, const std::vector<DrawRun>& runs,
                         Graphics::BindingGroupHandle frameGroup, Graphics::BindingGroupHandle viewGroup,
                         Graphics::BindingGroupHandle drawGroup, Graphics::BufferHandle skinned,
                         Graphics::PixelRect viewport, DrawPass pass, Graphics::BufferHandle commands = {}) const;
        /**
         * @brief Sizes view `view`'s command buffer and indirection for `runs` of `draws` this frame (ADR-0057),
         * imports them into `graph`, and appends the runs' command templates and group table entries to `templates` and
         * `groups`, the frame's; empty buffers, with nothing appended, when there are no runs. AddCullPasses then adds
         * the pass itself.
         */
        [[nodiscard]] CullView PrepareCull(RenderGraph& graph, std::uint64_t frameIndex, std::size_t view,
                                           const std::vector<ResolvedDraw>& draws, const std::vector<DrawRun>& runs,
                                           std::vector<Graphics::DrawIndexedIndirectCommand>& templates,
                                           std::vector<CullGroupEntry>& groups);
        /**
         * @brief Uploads the frame's `templates` and `groups` into their rings and adds one `Cull` pass per view of
         * `views` that has runs: a copy of its templates over its command buffer, then one dispatch of `builtin:cull`.
         */
        void AddCullPasses(RenderGraph& graph, std::uint64_t frameIndex, const std::vector<CullView>& views,
                           std::span<const Graphics::DrawIndexedIndirectCommand> templates,
                           std::span<const CullGroupEntry> groups);
        [[nodiscard]] Graphics::PipelineHandle MakeInternalPipeline(
            const ShaderContainer& container, const ShaderVariant* variant, Graphics::BindingGroupLayoutHandle layout,
            Graphics::TextureFormat colour, bool depthTested, std::uint32_t samples,
            Graphics::BindingGroupLayoutHandle setOneLayout = {},
            Graphics::BindingGroupLayoutHandle setZeroLayout = {});
        /**
         * @brief The Background pass's group sampling the Sky cube texture `cube` in frame `frameIndex`, made once
         * while it lives.
         */
        [[nodiscard]] Graphics::BindingGroupHandle SkyGroup(Graphics::TextureHandle cube, std::uint64_t frameIndex);
        /** @brief The material's group with the sampled targets in `replaced` bound to their defaults instead. */
        [[nodiscard]] Graphics::BindingGroupHandle GroupWithout(MaterialEntry& material,
                                                                const std::vector<AssetKey>& replaced);
        [[nodiscard]] Graphics::BindingGroupLayoutHandle MaterialLayoutOf(const ShaderVariant& variant);
        /** @brief The group binding `sceneColour` as Output's source in frame `frameIndex`, made while it lives. */
        [[nodiscard]] Graphics::BindingGroupHandle OutputGroup(Graphics::TextureHandle sceneColour,
                                                               std::uint64_t frameIndex);
        /**
         * @brief The group binding a view's reflection hits and history as Output's REFLECTIONS variant's sources in
         * frame `frameIndex`, made while both live.
         */
        [[nodiscard]] Graphics::BindingGroupHandle ReflectionOutputGroup(Graphics::TextureHandle hits,
                                                                         Graphics::TextureHandle history,
                                                                         std::uint64_t frameIndex);
        /** @brief Adds the pass that clears the window to black, for a frame with no view into it. */
        static void AddWindowClear(RenderGraph& graph, const FrameContext& frame);
        void DestroyPipelinesOf(const ShaderEntry& shader);
        void DestroyMaterial(MaterialEntry& material);
        void DestroyMesh(MeshEntry& mesh);
        /** @brief Packs every instance list into a new instance-list buffer, if one was prepared or released since. */
        void PackInstanceLists();
        /**
         * @brief The frame's draws, sorted; with compute skinning, each skinned mesh and palette range they draw is one
         * of `skins`, whose skinned vertices its draws read. Casting items have shadow pipelines when `shadowed`, and
         * point shadow pipelines when `pointShadowed`.
         */
        [[nodiscard]] std::vector<ResolvedDraw> ResolveDraws(std::vector<SkinDispatch>& skins, bool shadowed,
                                                             bool pointShadowed);
        /**
         * @brief Adds the Skinning pass running `skins` into the frame's skinned vertex buffer, imported into `graph`
         * as the returned resource; empty, with no pass, when there are none.
         */
        [[nodiscard]] GraphResource AddSkinningPass(RenderGraph& graph, std::uint64_t frameIndex,
                                                    const std::vector<SkinDispatch>& skins);
        /**
         * @brief The frame set's groups for frame `frameIndex`: its copy of the frame block, the sky's light, and each
         * shadow pool's records with what it binds this frame (its maps, else the map that lights everything).
         */
        [[nodiscard]] const FrameGroup& FrameGroupFor(std::uint64_t frameIndex);
        /** @brief What the scene's Sky lights with: its prepared cube texture, else its colour. */
        [[nodiscard]] SkySource SkySourceOf(const SceneLighting& lighting) const;
        /**
         * @brief The view set's group for slot `slot` of frame `frameIndex`'s view blocks, with `occlusion` as its
         * ambient occlusion, `shadow` as its shadow atlas (the map that lights everything when empty), `clusters` as
         * its light clusters (the buffer of one 0 when empty), and `reflectionHits` and `sceneHistory` as its
         * reflections' hit target and history (the black texture, zero confidence, when empty), made once while all
         * six live.
         */
        [[nodiscard]] Graphics::BindingGroupHandle ViewGroupFor(std::uint64_t frameIndex, std::size_t slot,
                                                                Graphics::TextureHandle occlusion,
                                                                Graphics::TextureHandle shadow = {},
                                                                Graphics::BufferHandle clusters = {},
                                                                Graphics::TextureHandle reflectionHits = {},
                                                                Graphics::TextureHandle sceneHistory = {});
        /**
         * @brief Adds view `view`'s LightClusters pass over the frame's `lightCount` lights into its cluster buffer,
         * imported into `graph`, which the view's lit passes then read; returns that buffer and its resource, or empty
         * ones when clustering is off, unavailable or there are no lights.
         */
        [[nodiscard]] std::pair<Graphics::BufferHandle, GraphResource> AddLightClustersPass(
            RenderGraph& graph, std::uint64_t frameIndex, std::size_t view, const ViewRequest& request,
            std::uint32_t lightCount, ClusterDepths depths);
        /**
         * @brief The draw set's group for frame `frameIndex`: its copies of the draw and palette buffers, the
         * instance-list buffer, and `indirection` at binding 3, a view's or (empty) the buffer that says there is none.
         */
        [[nodiscard]] Graphics::BindingGroupHandle DrawGroupFor(std::uint64_t frameIndex,
                                                                Graphics::BufferHandle indirection = {});
        [[nodiscard]] std::uint32_t NextId();

        Graphics::GraphicsDevice& m_device;
        ILogger& m_logger;
        /** @brief The scene targets' samples per pixel. */
        std::uint32_t m_samples;
        /** @brief RendererConfig::automaticInstancing: whether RunsOf merges matching draws. */
        bool m_automaticInstancing;
        /** @brief RendererConfig::computeSkinning: whether skinned draws read the Skinning pass's vertices. */
        bool m_computeSkinning;
        /** @brief RendererConfig::clusteredLights: whether views with lights get a LightClusters pass. */
        bool m_clusteredLights = true;
        /** @brief RendererConfig::gpuCulling: whether each view's cull pass gets the view's planes. */
        bool m_gpuCulling = true;
        /** @brief How each view's sun cascades are made (RendererConfig's shadow members). */
        CascadeConfig m_cascades;

        std::map<AssetKey, ShaderEntry> m_shaders;
        std::map<AssetKey, MeshEntry> m_meshes;
        std::map<AssetKey, MaterialEntry> m_materials;
        std::map<AssetKey, TargetEntry> m_targets;
        std::map<AssetKey, ImageEntry> m_images;
        /** @brief The Sky cubemaps and panoramas, as cube textures. */
        std::unique_ptr<SkyImages> m_skyImages;
        std::map<AssetKey, InstanceListEntry> m_instanceLists;
        /**
         * @brief Every instance list's copies, back to back (`Fenrir/Draw.glsl`'s FenrirInstance), at set 3 binding 1:
         * one zeroed record when there are none.
         */
        Graphics::BufferHandle m_instanceListBuffer;
        /** @brief Its size in bytes. */
        std::size_t m_instanceListBytes = 0;
        /** @brief Whether a list was prepared or released since the instance-list buffer was packed. */
        bool m_instancesChanged = true;
        /** @brief The samplers made for material parameters, by description. */
        std::vector<std::pair<SamplerDescription, Graphics::SamplerHandle>> m_samplers;
        std::map<PipelineKey, PipelineEntry> m_pipelines;
        std::map<PrepassKey, PrepassEntry> m_prepassPipelines;
        /** @brief The shadow pipelines, made as the prepass's are: the item's vertex stage, then builtin:shadow's. */
        std::map<ShadowKey, PrepassEntry> m_shadowPipelines;
        std::uint32_t m_nextId = 1;

        /** @brief The fallback material: its program, laid out as any shader, and drawn with no material group. */
        ShaderEntry m_fallback;
        ShaderContainer m_background;
        ShaderContainer m_output;
        /** @brief `builtin:depth-prepass`, whose fragment stages the prepass pipelines run. */
        ShaderContainer m_prepass;
        /** @brief `builtin:shadow`, whose fragment stages the shadow pipelines run. */
        ShaderContainer m_shadow;
        /** @brief `builtin:skinning`, the Skinning pass's compute program, and its pipeline; empty if it failed. */
        ShaderContainer m_skinning;
        Graphics::PipelineHandle m_skinningPipeline;
        /** @brief Its sets: 0 a dispatch's job block, 1 the mesh's vertices, 2 the palette and skinned vertices. */
        Graphics::BindingGroupLayoutHandle m_skinJobLayout;
        Graphics::BindingGroupLayoutHandle m_skinSourceLayout;
        Graphics::BindingGroupLayoutHandle m_skinTargetLayout;
        /**
         * @brief `builtin:light-clusters`, the LightClusters passes' compute program, and its pipeline; empty if it
         * failed.
         */
        ShaderContainer m_lightClusters;
        Graphics::PipelineHandle m_lightClustersPipeline;
        /** @brief Its sets: 0 the view's job block, 1 the frame's light buffer and the view's cluster buffer. */
        Graphics::BindingGroupLayoutHandle m_clusterJobLayout;
        Graphics::BindingGroupLayoutHandle m_clusterTargetLayout;
        /** @brief `builtin:cull`, the Cull passes' compute program. */
        ShaderContainer m_cull;
        /** @brief Its pipeline; empty if it failed, and then every pass draws directly. */
        Graphics::PipelineHandle m_cullPipeline;
        /** @brief Its set 0: the view's job block. */
        Graphics::BindingGroupLayoutHandle m_cullJobLayout;
        /** @brief Its set 1: the frame's draw records and group table, and the view's command buffer and indirection.
         */
        Graphics::BindingGroupLayoutHandle m_cullTargetLayout;

        Graphics::BindingGroupLayoutHandle m_frameLayout;
        Graphics::BindingGroupLayoutHandle m_viewLayout;
        Graphics::BindingGroupLayoutHandle m_drawLayout;
        Graphics::BindingGroupLayoutHandle m_backgroundLayout;
        /** @brief Set 2 of the Background pass's CUBEMAP variant: the Sky cubemap. */
        Graphics::BindingGroupLayoutHandle m_skyLayout;
        /** @brief Set 2 of a Masked item's prepass pipeline: its block (alpha, cutoff) and base colour texture. */
        Graphics::BindingGroupLayoutHandle m_prepassMaskLayout;
        /** @brief Set 2 of the Output pass: the view's SceneColor and its sampler. */
        Graphics::BindingGroupLayoutHandle m_outputLayout;
        /** @brief Set 2 of Output's REFLECTIONS variant: the view's reflection hits and history. */
        Graphics::BindingGroupLayoutHandle m_reflectionOutputLayout;
        /**
         * @brief Set 1 of the Output pass: the Output block, the view's exposure, vignette, colour grade, tonemapper,
         * film grain, chromatic aberration and grey channel.
         */
        Graphics::BindingGroupLayoutHandle m_outputBlockLayout;
        Graphics::PipelineHandle m_backgroundPipeline;
        Graphics::PipelineHandle m_skyPipeline;
        Graphics::PipelineHandle m_outputPipeline;
        /** @brief Output's OCCLUSION variant, which draws one channel of a texture as grey. */
        Graphics::PipelineHandle m_occlusionOutputPipeline;
        /** @brief Output's REFLECTIONS variant, which draws last frame's history at a view's reflection hits. */
        Graphics::PipelineHandle m_reflectionOutputPipeline;
        /** @brief Output's ABERRATION variant (MSAA too with MSAA), which reads each channel at its own point. */
        Graphics::PipelineHandle m_aberrationOutputPipeline;

        /** @brief The default textures, by DefaultTexture: white, black and the flat normal. */
        std::array<Graphics::TextureHandle, 3> m_defaultTextures{};
        /** @brief The rect lights' LTC tables (ADR-0048): the matrix's at set 0 binding 5, the amplitude's at 6. */
        Graphics::TextureHandle m_ltcMatrix;
        Graphics::TextureHandle m_ltcAmplitude;
        Graphics::SamplerHandle m_defaultSampler;
        Graphics::SamplerHandle m_outputSampler;
        Graphics::SamplerHandle m_skySampler;
        /** @brief Reads a shadow atlas by hardware comparison, bilinearly, clamped (ADR-0050). */
        Graphics::SamplerHandle m_shadowSampler;
        /** @brief The 1 x 1 shadow map of depth 0 that lights everything, bound where a view has no shadow. */
        Graphics::TextureHandle m_noShadow;
        /**
         * @brief The spot shadow pool (RendererConfig's spot shadow members): the spot shadow atlas, and the records
         * set 0 binds at binding 7 beside it at binding 8.
         */
        std::unique_ptr<ShadowPool<SpotShadowKind>> m_spotShadows;
        /**
         * @brief The point shadow pool (RendererConfig's point shadow members): the point shadow cubes, and the
         * records set 0 binds at binding 9 beside them at binding 10.
         */
        std::unique_ptr<ShadowPool<PointShadowKind>> m_pointShadows;
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
        /** @brief Each LightClusters pass's job block, one uniform slot a view, bound at its set 0. */
        std::unique_ptr<BlockRing> m_clusterJobs;
        /** @brief A view's cluster buffers, a copy per frame in flight, and how many bytes each holds. */
        struct ClusterBuffers
        {
            std::unique_ptr<FrameRingBuffer> copies;
            std::size_t capacity = 0;
        };
        /** @brief Each view's cluster buffers, by view index. */
        std::vector<ClusterBuffers> m_clusterBuffers;
        /** @brief The buffer of one 0 that view blocks without clusters bind at set 1 binding 3. */
        Graphics::BufferHandle m_noClusters;
        /** @brief Each Cull pass's job block, one uniform slot a view, bound at its set 0. */
        std::unique_ptr<BlockRing> m_cullJobs;
        /** @brief The frame's command templates, which each Cull pass copies its view's from. */
        GrownRing m_cullTemplates;
        /** @brief The frame's group table, which each Cull pass reads its view's groups of. */
        GrownRing m_cullGroups;
        /** @brief Each view's cull rings, by view index. */
        std::vector<CullRings> m_cullRings;
        /**
         * @brief The buffer of one entry naming no record (`Fenrir/DrawRecord.glsl`'s FenrirNoIndirection) that the
         * draw group of every direct draw binds at set 3 binding 3.
         */
        Graphics::BufferHandle m_noIndirection;
        /**
         * @brief builtin:cull's set 1 groups, by the draw buffer, the group table, the command buffer and the
         * indirection, while all four live.
         */
        BindingGroupCache<5> m_cullTargetGroups;
        /** @brief builtin:light-clusters' set 1 groups, by the light buffer and the cluster buffer, while both live. */
        BindingGroupCache<2> m_clusterTargetGroups;
        /** @brief The Background pass's group of each Sky cube texture it draws, while the texture lives. */
        BindingGroupCache<1> m_skyGroups;
        /** @brief The sky filtered into image-based lighting, which set 0 binds. */
        std::unique_ptr<SkyLighting> m_skyLighting;
        /** @brief Each view's bloom passes, in `PostProcess`. */
        std::unique_ptr<BloomChain> m_bloom;
        /** @brief The GTAO passes of each view that has ambient occlusion, after its prepass. */
        std::unique_ptr<Gtao> m_gtao;
        /** @brief Each reflecting camera's history colour and Hi-Z pyramids, kept across frames, and their passes. */
        std::unique_ptr<SceneHistory> m_sceneHistory;
        /** @brief The reflection trace of each view with reflections, after its HiZ passes. */
        std::unique_ptr<ReflectionTrace> m_reflectionTrace;
        /** @brief Each copy's frame set group, by frame index modulo the copies. */
        std::vector<FrameGroup> m_frameGroups;
        /**
         * @brief The draw set's groups, by the draw buffer, the palette, the instance-list buffer and the indirection,
         * while all four live.
         */
        BindingGroupCache<4> m_drawGroups;
        /**
         * @brief The view set's groups, by the view ring's buffer, slot, the occlusion and shadow textures, the
         * cluster buffer and the reflection hits and history, while all six live.
         */
        BindingGroupCache<7> m_viewGroups;
        /** @brief The Output pass's binding group for each SceneColor texture, while the texture lives. */
        BindingGroupCache<1> m_outputGroups;
        /** @brief The REFLECTIONS variant's binding group for each hit target and history, while both live. */
        BindingGroupCache<2> m_reflectionOutputGroups;

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
