#pragma once

// SPIKE (prototype/fenrir-renderer-spike, throwaway): the renderer core of ADR-0019. No FenrirApp, FenrirECS or
// FenrirScene: the facade hands it asset data as (key, revision, data), and each frame a plain-data FrameInput.

#include "Device/Device.hpp"
#include "RenderGraph.hpp"
#include "ShaderContainer.hpp"

#include "FenrirAssets/Assets.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace Fenrir::Render
{
    using AssetKey = std::uint64_t; // index << 32 | generation; 0 is none

    struct DrawItemInput
    {
        AssetKey mesh = 0;
        AssetKey material = 0;
        Math::Mat4 world{1.0f};
    };

    struct ViewInput
    {
        Math::Mat4 view{1.0f};
        Math::Mat4 projection{1.0f};
        Math::Vec3 position{0.0f};
        float viewport[4] = {0, 0, 1, 1}; // normalised, from the top-left
        bool drawDebug = true;
        bool hasClearColour = false;
        Colour clearColour{};
    };

    struct FrameInput
    {
        std::vector<DrawItemInput> items;
        std::vector<ViewInput> views; // in camera order
        bool hasLight = false;
        Math::Vec3 lightDirection{0, -1, 0};
        Colour lightColour{};
        float lightIntensity = 1.0f;
        Colour ambient{0.03f, 0.03f, 0.03f, 1};
        float ambientIntensity = 1.0f;
        Colour skyColour{0.1f, 0.1f, 0.12f, 1};
        AssetKey skyCubemap = 0;
        std::span<const DebugLineVertex> lines;
        float time = 0.0f;
    };

    struct FrameStats
    {
        std::uint32_t items = 0;
        std::uint32_t draws = 0;
        std::uint32_t passes = 0;
        std::uint32_t culledPasses = 0;
        std::uint32_t uploads = 0;
        std::uint32_t pipelines = 0;
        std::uint32_t liveBuffers = 0;
        std::uint32_t liveTextures = 0;
        std::vector<std::string> passOrder;
    };

    /** @brief Reversed-Z (near 1, infinity 0), [0, 1] clip, NDC +y up (ADR-0026). */
    Math::Mat4 ReversedInfinitePerspective(float fovY, float aspect, float near);

    class RendererCore
    {
      public:
        RendererCore(std::unique_ptr<Gpu::GraphicsDevice> device, std::function<void(const std::string&)> log);
        ~RendererCore();

        bool Init(std::string& error);

        [[nodiscard]] bool IsMeshCurrent(AssetKey key, std::uint32_t revision) const;
        void PrepareMesh(AssetKey key, std::uint32_t revision, const MeshData& mesh);
        [[nodiscard]] bool IsImageCurrent(AssetKey key, std::uint32_t revision) const;
        void PrepareImage(AssetKey key, std::uint32_t revision, const ImageData& image);
        [[nodiscard]] bool IsMaterialCurrent(AssetKey key, std::uint32_t revision) const;
        /** @brief Images the material names must already be prepared. */
        void PrepareMaterial(AssetKey key, std::uint32_t revision, const MaterialData& material);

        FrameStats RenderFrame(const FrameInput& input);
        /** @brief Advances the frame index without drawing (a minimised window, ADR-0022). */
        void SkipFrame();

        bool SetSwapInterval(int interval);
        std::vector<std::uint8_t> ReadBackbuffer(int& width, int& height);

        static AssetKey Key(std::uint32_t index, std::uint32_t generation)
        {
            return generation == 0 ? 0 : (static_cast<AssetKey>(index) << 32) | generation;
        }

      private:
        struct Program
        {
            std::string name;
            ShaderContainer container;
            Gpu::ShaderHandle vertex;
            Gpu::ShaderHandle fragment;
            std::array<Gpu::BindGroupLayoutHandle, 4> sets{};
            const ReflectedBinding* params = nullptr; // set 2, binding 0, if a block
        };
        struct GpuMesh
        {
            std::uint32_t revision = 0;
            Gpu::BufferHandle vertices;
            Gpu::BufferHandle indices;
            std::uint32_t indexCount = 0;
        };
        struct GpuImage
        {
            std::uint32_t revision = 0;
            Gpu::TextureHandle texture;
            bool cube = false;
        };
        struct GpuMaterial
        {
            std::uint32_t revision = 0;
            std::size_t program = 0;
            BlendMode blend = BlendMode::Opaque;
            bool doubleSided = false;
            Gpu::BufferHandle params;
            Gpu::BindGroupHandle group;
        };
        struct FrameSlot
        {
            Gpu::BufferHandle frameUbo;
            Gpu::BindGroupHandle frameGroup;
            Gpu::BufferHandle viewUbo;
            std::vector<Gpu::BindGroupHandle> viewGroups;
            Gpu::BufferHandle draws;
            std::size_t drawCapacity = 0;
            Gpu::BindGroupHandle drawGroup;
            Gpu::BufferHandle lines;
            std::size_t lineCapacity = 0;
        };

        bool LoadProgram(const char* name, std::span<const std::uint8_t> bytes, std::string& error);
        std::size_t ProgramIndex(const std::string& name) const;
        Gpu::PipelineHandle MeshPipeline(std::size_t program, BlendMode blend, bool doubleSided);
        void LogOnce(const std::string& message);
        Gpu::BindGroupHandle MakeMaterialGroup(const Program& program, Gpu::BufferHandle params,
                                               const MaterialData* material);

        std::unique_ptr<Gpu::GraphicsDevice> m_device;
        std::function<void(const std::string&)> m_log;
        std::set<std::string> m_logged;
        std::uint64_t m_frame = 0;

        std::vector<Program> m_programs;
        Gpu::BindGroupLayoutHandle m_frameLayout, m_viewLayout, m_drawLayout, m_emptyLayout;
        Gpu::TextureHandle m_white;
        Gpu::TextureHandle m_blackCube;
        Gpu::SamplerHandle m_repeatSampler, m_clampSampler;
        Gpu::PipelineHandle m_skyPipeline, m_linePipeline, m_outputPipeline;
        GpuMaterial m_fallback;
        std::array<FrameSlot, 2> m_slots{};
        std::map<std::tuple<std::size_t, int, bool>, Gpu::PipelineHandle> m_pipelines;
        std::unordered_map<AssetKey, GpuMesh> m_meshes;
        std::unordered_map<AssetKey, GpuImage> m_images;
        std::unordered_map<AssetKey, GpuMaterial> m_materials;
        TransientPool m_transients;
        std::uint32_t m_uploads = 0;
    };
} // namespace Fenrir::Render
