#pragma once

// SPIKE: ADR-0019's render graph, cut down. Passes declare what they read and write; the graph orders them by render
// stage then registration, culls passes nothing needed, checks reads, takes transients from a pool keyed by
// description, and records each pass into its own CommandList before submitting them in order.

#include "Device/Device.hpp"

#include <functional>
#include <string>
#include <vector>

namespace Fenrir::Render
{
    enum class RenderStage
    {
        Shadow,
        Opaque,
        Background,
        Transparent,
        Debug,
        PostProcess,
        Output,
        Overlay,
    };

    struct GraphTexture
    {
        std::uint32_t id = UINT32_MAX;
    };

    struct TransientDesc
    {
        Gpu::TextureFormat format = Gpu::TextureFormat::Rgba16Float;
        int width = 0;
        int height = 0;
        friend bool operator==(const TransientDesc&, const TransientDesc&) = default;
    };

    class TransientPool
    {
      public:
        Gpu::TextureHandle Acquire(Gpu::GraphicsDevice& device, const TransientDesc& desc, std::uint64_t frame);
        void EndFrame(Gpu::GraphicsDevice& device, std::uint64_t frame);
        [[nodiscard]] std::size_t Size() const
        {
            return m_entries.size();
        }

      private:
        struct Entry
        {
            TransientDesc desc;
            Gpu::TextureHandle texture;
            std::uint64_t lastUsed = 0;
            bool inUse = false;
        };
        std::vector<Entry> m_entries;
    };

    struct PassContext
    {
        Gpu::CommandList& list;
        std::function<Gpu::TextureHandle(GraphTexture)> texture;
    };

    class PassBuilder;

    class RenderGraph
    {
      public:
        GraphTexture ImportBackbuffer(int width, int height);
        GraphTexture CreateTransient(std::string name, TransientDesc desc);

        void AddPass(std::string name, RenderStage stage, const std::function<void(PassBuilder&)>& setup,
                     std::function<void(PassContext&)> record);

        struct Result
        {
            std::uint32_t passesRun = 0;
            std::uint32_t passesCulled = 0;
            std::vector<std::string> errors;
            std::vector<std::string> order;
        };
        /** @brief Compiles and runs the graph: one CommandList per pass, submitted in order. */
        Result Execute(Gpu::GraphicsDevice& device, TransientPool& pool, std::uint64_t frame);

      private:
        friend class PassBuilder;
        struct Resource
        {
            std::string name;
            TransientDesc desc;
            bool imported = false;
        };
        struct Access
        {
            std::uint32_t resource;
            Gpu::Usage usage;
            bool write;
        };
        struct Pass
        {
            std::string name;
            RenderStage stage;
            std::vector<Access> accesses;
            std::function<void(PassContext&)> record;
        };
        std::vector<Resource> m_resources;
        std::vector<Pass> m_passes;
    };

    class PassBuilder
    {
      public:
        /** @brief Writes the texture; `load` keeps what an earlier pass wrote, so it also reads it. */
        void Write(GraphTexture t, Gpu::Usage usage, bool load)
        {
            m_pass.accesses.push_back({t.id, usage, true});
            if (load)
                m_pass.accesses.push_back({t.id, usage, false});
        }
        void Read(GraphTexture t, Gpu::Usage usage)
        {
            m_pass.accesses.push_back({t.id, usage, false});
        }

      private:
        friend class RenderGraph;
        explicit PassBuilder(RenderGraph::Pass& pass) : m_pass(pass)
        {
        }
        RenderGraph::Pass& m_pass;
    };
} // namespace Fenrir::Render
