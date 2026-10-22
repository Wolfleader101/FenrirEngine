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

#include "Core/FrameRingBuffer.hpp"
#include "Core/RenderFeature.hpp"
#include "Core/SceneFrame.hpp"
#include "Device/GraphicsDevice.hpp"
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
        /** @brief Meshes and material blocks uploaded since the last call. */
        std::uint32_t uploads = 0;
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
     * shader, vertex layout and culling while the shader lives, and sorted by a 64-bit key (render stage in the high
     * bits; then pipeline, material and mesh). Each view gets transient `SceneColor` (linear RGBA16F) and `SceneDepth`
     * (32-bit float, reversed-Z, cleared to 0) and three passes: `Opaque`, one draw per item; `Background`, its clear
     * colour at depth 0; and `Output`, SceneColor clamped into the backbuffer. With no view the backbuffer is cleared
     * to black.
     *
     * **Bindings.** Sets 0 (frame), 1 (view) and 3 (draw) are declared here as `Fenrir/Frame.glsl`, `View.glsl` and
     * `Draw.glsl` declare them; in debug builds a shader whose reflection disagrees fails pipeline creation with a
     * diagnostic. Set 2, the material, is laid out from the shader container's reflection: its `MaterialParams` block
     * is the default block image with the material's parameter overrides written over it, and each texture binds the
     * renderer's default texture its parameter default names. A vertex stream the shader reads but the mesh lacks binds
     * a constant default (normal +Y, UV 0, colour white, tangent +X), logged once per mesh and stream.
     */
    class SceneRenderer final : public RenderFeature
    {
      public:
        /** @brief Makes the default textures, the fixed sets' layouts and the internal pipelines on `device`. */
        SceneRenderer(Graphics::GraphicsDevice& device, ILogger& logger);
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

        /** @brief The keys with GPU resources, for the facade to release those that went stale. */
        [[nodiscard]] std::vector<AssetKey> ShaderKeys() const;
        [[nodiscard]] std::vector<AssetKey> MeshKeys() const;
        [[nodiscard]] std::vector<AssetKey> MaterialKeys() const;
        /** @} */

        /** @brief What the next frame draws. */
        void SetFrame(SceneFrame frame);

        void AddPasses(RenderGraph& graph, const FrameContext& frame) override;

        /** @brief The last frame's draws, and the uploads since the last call. */
        [[nodiscard]] SceneStats TakeStats();

      private:
        friend struct Fenrir::RendererProbe;

        /** @brief A prepared shader: its container, the variant drawn and set 2's layout. */
        struct ShaderEntry
        {
            std::uint32_t revision = 0;
            ShaderContainer container;
            /** @brief The base variant; null if the container has none. */
            const ShaderVariant* variant = nullptr;
            /** @brief Set 2's layout, from reflection; empty when the variant binds nothing there. */
            Graphics::BindingGroupLayoutHandle materialLayout;
            /** @brief The number in sort keys, unique among live shaders. */
            std::uint32_t id = 0;
        };

        /** @brief A prepared mesh: its streams back to back in one buffer, and its indices. */
        struct MeshEntry
        {
            std::uint32_t revision = 0;
            Graphics::BufferHandle vertices;
            Graphics::BufferHandle indices;
            std::uint32_t indexCount = 0;
            /** @brief Each standard stream's offset in `vertices`, by vertex location; empty if the mesh lacks it. */
            std::array<std::optional<std::size_t>, 6> streams{};
            std::uint32_t id = 0;
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
            Graphics::BufferHandle block;
            Graphics::BindingGroupHandle group;
            bool doubleSided = false;
            std::uint32_t id = 0;
        };

        /** @brief What a mesh pipeline is made for: the program, which read streams the mesh has, and culling. */
        struct PipelineKey
        {
            const ShaderEntry* shader = nullptr;
            std::uint32_t streams = 0;
            bool doubleSided = false;

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
            Graphics::BindingGroupHandle material;
            const MeshEntry* mesh = nullptr;
            Math::Mat4 world{1.0f};
        };

        /**
         * @brief Uniform or storage blocks written each frame, in one ring buffer per frame in flight (ADR-0019), with
         * a binding group per copy and slot. It grows as more slots are needed.
         */
        class BlockRing
        {
          public:
            BlockRing(Graphics::GraphicsDevice& device, Graphics::BindingGroupLayoutHandle layout, bool storage,
                      std::size_t slotSize, std::string label);
            ~BlockRing();
            BlockRing(const BlockRing&) = delete;
            BlockRing& operator=(const BlockRing&) = delete;
            BlockRing(BlockRing&&) = delete;
            BlockRing& operator=(BlockRing&&) = delete;

            /** @brief Makes room for `slots` slots, remaking the buffers if there is not. */
            void Reserve(std::size_t slots);
            /** @brief Writes `bytes` at slot `slot` of frame `frameIndex`'s copy. */
            void Write(std::uint64_t frameIndex, std::size_t slot, std::span<const std::byte> bytes);
            /**
             * @brief The binding group of slot `slot` in frame `frameIndex`'s copy; a storage ring has one group per
             * copy spanning every slot.
             */
            [[nodiscard]] Graphics::BindingGroupHandle Group(std::uint64_t frameIndex, std::size_t slot) const;

          private:
            void DestroyGroups();

            Graphics::GraphicsDevice& m_device;
            Graphics::BindingGroupLayoutHandle m_layout;
            bool m_storage;
            std::size_t m_slotSize;
            std::string m_label;
            std::size_t m_slots = 0;
            std::unique_ptr<FrameRingBuffer> m_ring;
            /** @brief By copy, then slot (one slot for a storage ring). */
            std::vector<std::vector<Graphics::BindingGroupHandle>> m_groups;
        };

        [[nodiscard]] const PipelineEntry& MeshPipeline(const ShaderEntry& shader, const MeshEntry& mesh,
                                                        bool doubleSided);
        [[nodiscard]] Graphics::PipelineHandle MakeInternalPipeline(const ShaderContainer& container,
                                                                    Graphics::BindingGroupLayoutHandle layout,
                                                                    Graphics::TextureFormat colour, bool depthTested);
        [[nodiscard]] Graphics::BindingGroupLayoutHandle MaterialLayoutOf(const ShaderVariant& variant);
        [[nodiscard]] Graphics::BindingGroupHandle OutputGroup(Graphics::TextureHandle sceneColour);
        void DestroyPipelinesOf(const ShaderEntry& shader);
        void DestroyMaterial(MaterialEntry& material);
        void DestroyMesh(MeshEntry& mesh);
        [[nodiscard]] std::vector<ResolvedDraw> ResolveDraws();
        [[nodiscard]] std::uint32_t NextId();

        Graphics::GraphicsDevice& m_device;
        ILogger& m_logger;

        std::map<AssetKey, ShaderEntry> m_shaders;
        std::map<AssetKey, MeshEntry> m_meshes;
        std::map<AssetKey, MaterialEntry> m_materials;
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
        Graphics::BindingGroupLayoutHandle m_outputLayout;
        Graphics::PipelineHandle m_backgroundPipeline;
        Graphics::PipelineHandle m_outputPipeline;

        /** @brief The default textures, by DefaultTexture: white, black and the flat normal. */
        std::array<Graphics::TextureHandle, 3> m_defaultTextures{};
        Graphics::SamplerHandle m_defaultSampler;
        Graphics::SamplerHandle m_outputSampler;
        /** @brief The constant every vertex stream a mesh lacks reads, at stride 0. */
        Graphics::BufferHandle m_defaultStreams;

        std::unique_ptr<BlockRing> m_frameBlocks;
        std::unique_ptr<BlockRing> m_viewBlocks;
        std::unique_ptr<BlockRing> m_backgroundBlocks;
        std::unique_ptr<BlockRing> m_drawBlocks;
        /** @brief The Output pass's binding group for each SceneColor texture, while the texture lives. */
        std::map<std::pair<std::uint32_t, std::uint32_t>, Graphics::BindingGroupHandle> m_outputGroups;

        SceneFrame m_frame;
        SceneStats m_stats;
        /** @brief The missing vertex streams already logged, by mesh and vertex location. */
        std::set<std::pair<AssetKey, std::uint32_t>> m_loggedStreams;
    };
} // namespace Fenrir::Render
