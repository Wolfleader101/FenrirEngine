#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "Device/CommandList.hpp"
#include "Device/GraphicsDevice.hpp"
#include "Device/Resources.hpp"

namespace Fenrir::Render
{
    class TransientPool;

    /**
     * @brief Where in the frame a render pass runs. Passes run in this order and, within a stage, in registration
     * order, unless one reads what a later one writes (ADR-0019).
     */
    enum class RenderStage : std::uint8_t
    {
        /** @brief Work whose result is kept across frames, such as a panorama sky's cube texture, made when needed. */
        Prepare,
        Shadow,
        Opaque,
        Background,
        Transparent,
        Debug,
        PostProcess,
        Output,
        Overlay,
    };

    /** @brief A texture or buffer of one frame's render graph, transient or imported. Empty when made with no args. */
    struct GraphResource
    {
        std::uint32_t id = std::numeric_limits<std::uint32_t>::max();

        /** @brief Whether this names nothing. */
        [[nodiscard]] constexpr bool IsEmpty() const noexcept
        {
            return id == std::numeric_limits<std::uint32_t>::max();
        }

        friend constexpr bool operator==(const GraphResource&, const GraphResource&) = default;
    };

    class RenderGraph;

    /**
     * @brief Declares, while a render pass is added, every graph resource it uses and how. The declarations order and
     * cull the passes, size the transients' lifetimes, and become the device pass's attachments and declared uses.
     */
    class PassBuilder
    {
      public:
        /**
         * @brief Draws into `texture` as the next colour attachment. Loading it also reads what earlier passes wrote;
         * clearing it, or not caring, writes it whole.
         */
        void ColourAttachment(GraphResource texture, Graphics::LoadOp load = Graphics::LoadOp::Clear,
                              std::array<float, 4> clear = {0.0f, 0.0f, 0.0f, 1.0f});
        /**
         * @brief Draws into one mip level of one layer (a cube texture's face) of `texture` as the next colour
         * attachment, covering all of it: what it held is not loaded.
         */
        void ColourAttachmentLevel(GraphResource texture, std::uint32_t mipLevel, std::uint32_t layer = 0);
        /** @brief Tests and writes depth in `texture`; loading it also reads it. Clears to `clear` (0 is far). */
        void DepthAttachment(GraphResource texture, Graphics::LoadOp load = Graphics::LoadOp::Clear,
                             float clear = 0.0f);
        /** @brief Tests depth in `texture` without writing it. */
        void DepthReadOnly(GraphResource texture);
        /** @brief Samples `texture` in a shader. */
        void Sample(GraphResource texture);
        /** @brief Reads `resource` as storage in a shader. */
        void StorageRead(GraphResource resource);
        /** @brief Writes `resource` as storage in a shader. */
        void StorageWrite(GraphResource resource);
        /** @brief Copies from `resource`. */
        void CopySource(GraphResource resource);
        /** @brief Copies into `resource`. */
        void CopyDestination(GraphResource resource);

      private:
        friend class RenderGraph;

        explicit PassBuilder(RenderGraph& graph, std::uint32_t pass) : m_graph(graph), m_pass(pass)
        {
        }

        void Use(GraphResource resource, Graphics::ResourceUsage usage, bool reads, bool writes);

        RenderGraph& m_graph;
        std::uint32_t m_pass;
    };

    /** @brief What a render pass's record function records with: its encoder and its resources' device handles. */
    class PassContext
    {
      public:
        /** @brief Records into the pass's own CommandList; the pass is already begun with its attachments. */
        [[nodiscard]] Graphics::PassEncoder& Encoder() const noexcept
        {
            return m_encoder;
        }

        /** @brief The device texture behind a graph resource the pass declared. Empty for a buffer. */
        [[nodiscard]] Graphics::TextureHandle Texture(GraphResource resource) const;

        /** @brief The device buffer behind a graph resource the pass declared. Empty for a texture. */
        [[nodiscard]] Graphics::BufferHandle Buffer(GraphResource resource) const;

        /** @brief The frame being recorded. */
        [[nodiscard]] std::uint64_t FrameIndex() const noexcept
        {
            return m_frameIndex;
        }

      private:
        friend class RenderGraph;

        PassContext(Graphics::PassEncoder& encoder, const std::vector<Graphics::TextureHandle>& textures,
                    const std::vector<Graphics::BufferHandle>& buffers, std::uint64_t frameIndex)
            : m_encoder(encoder), m_textures(textures), m_buffers(buffers), m_frameIndex(frameIndex)
        {
        }

        Graphics::PassEncoder& m_encoder;
        const std::vector<Graphics::TextureHandle>& m_textures;
        const std::vector<Graphics::BufferHandle>& m_buffers;
        std::uint64_t m_frameIndex;
    };

    /** @brief The order a compiled render graph runs its passes in, what it culled, and what was wrong with it. */
    struct GraphCompilation
    {
        /** @brief The passes that run, by registration index, in the order they run. */
        std::vector<std::uint32_t> order;
        /** @brief The passes culled because nothing reads what they write, by registration index. */
        std::vector<std::uint32_t> culled;
        /** @brief One line per error. With any, nothing should be recorded. */
        std::vector<std::string> errors;

        /** @brief Whether the graph compiled without errors. */
        [[nodiscard]] bool Ok() const noexcept
        {
            return errors.empty();
        }

      private:
        friend class RenderGraph;

        /** @brief Each resource's first and last position in `order`, or none if no running pass uses it. */
        struct Lifetime
        {
            std::size_t first = 0;
            std::size_t last = 0;
            bool used = false;
        };
        std::vector<Lifetime> m_lifetimes;
    };

    /** @brief One recorded render pass: its name and stage, and the CommandList its record function filled. */
    struct RecordedPass
    {
        std::string name;
        RenderStage stage = RenderStage::Opaque;
        Graphics::CommandList commands;
    };

    /**
     * @brief The render passes of one frame and the resources they use (ADR-0019), built and compiled every frame.
     *
     * Passes run by render stage, then registration order; a pass that reads a resource only later passes write runs
     * after them, as when one view samples the image another view draws. Passes whose writes nothing reads are culled,
     * keeping those that write an imported resource, and a pass reading what nothing writes is an error. Transients
     * come from a TransientPool keyed by description, taken at their first use and given back after their last, so two
     * with equal descriptions and disjoint lifetimes share one texture. Imported resources are never pooled.
     *
     * Designed for an explicit backend, not built (ADR-0019):
     * - memory aliasing: lifetimes are computed here, but transients only share a pool entry, never memory;
     * - async compute and copy queues: passes carry a QueueKind, and Record runs them all in order;
     * - parallel recording: each pass records into its own CommandList, but one thread calls every record function;
     * - split barriers: uses are declared per pass and go down in its PassDesc; no barrier is split across passes;
     * - a depth prepass for transparent items, which would be one more pass in `Transparent`.
     * Persistent pipeline caches belong to the device, and a render thread would take the draw list as its snapshot.
     */
    class RenderGraph
    {
      public:
        /** @brief Brings in a texture made outside the graph, such as the backbuffer or a camera's image target. */
        GraphResource ImportTexture(std::string name, Graphics::TextureHandle texture,
                                    const Graphics::TextureDesc& desc);
        /** @brief Brings in a buffer made outside the graph. */
        GraphResource ImportBuffer(std::string name, Graphics::BufferHandle buffer, const Graphics::BufferDesc& desc);
        /** @brief A texture that lives for this frame only, from the pool. Its usage flags gain what passes declare. */
        GraphResource CreateTexture(std::string name, const Graphics::TextureDesc& desc);
        /** @brief A buffer that lives for this frame only, from the pool. Its usage flags gain what passes declare. */
        GraphResource CreateBuffer(std::string name, const Graphics::BufferDesc& desc);

        /**
         * @brief Adds a render pass: `setup` declares its resources through the builder at once, and `record` records
         * its commands when the graph is recorded, if the pass is not culled.
         */
        void AddPass(std::string name, RenderStage stage, Graphics::QueueKind queue,
                     const std::function<void(PassBuilder&)>& setup, std::function<void(PassContext&)> record);

        /** @brief Orders the passes, culls the unread ones and checks every read has a writer. */
        [[nodiscard]] GraphCompilation Compile() const;

        /**
         * @brief Records the compiled graph's passes in order, each into its own CommandList, taking transients from
         * `pool` for `frameIndex`. Records nothing if the compilation has errors.
         */
        [[nodiscard]] std::vector<RecordedPass> Record(const GraphCompilation& compilation,
                                                       Graphics::GraphicsDevice& device, TransientPool& pool,
                                                       std::uint64_t frameIndex);

        /** @brief A pass's name, by registration index. */
        [[nodiscard]] const std::string& PassName(std::uint32_t pass) const;

        /** @brief How many passes were added. */
        [[nodiscard]] std::size_t PassCount() const noexcept
        {
            return m_passes.size();
        }

      private:
        friend class PassBuilder;

        struct Resource
        {
            std::string name;
            std::variant<Graphics::TextureDesc, Graphics::BufferDesc> desc;
            /** @brief Empty for a transient. */
            Graphics::TextureHandle importedTexture;
            Graphics::BufferHandle importedBuffer;
            bool imported = false;

            [[nodiscard]] bool IsTexture() const noexcept
            {
                return std::holds_alternative<Graphics::TextureDesc>(desc);
            }
        };

        struct Access
        {
            std::uint32_t resource = 0;
            Graphics::ResourceUsage usage = Graphics::ResourceUsage::Sampled;
            bool reads = false;
            bool writes = false;
            Graphics::LoadOp load = Graphics::LoadOp::Clear;
            std::array<float, 4> clearColour{};
            float clearDepth = 0.0f;
            /** @brief A colour attachment's mip level and layer. */
            std::uint32_t mipLevel = 0;
            std::uint32_t layer = 0;
        };

        struct Pass
        {
            std::string name;
            RenderStage stage = RenderStage::Opaque;
            Graphics::QueueKind queue = Graphics::QueueKind::Graphics;
            std::vector<Access> accesses;
            std::function<void(PassContext&)> record;
        };

        [[nodiscard]] std::vector<std::uint32_t> OrderPasses(std::vector<std::string>& errors) const;
        [[nodiscard]] Graphics::PassDesc MakePassDesc(const Pass& pass,
                                                      const std::vector<Graphics::TextureHandle>& textures,
                                                      const std::vector<Graphics::BufferHandle>& buffers) const;

        std::vector<Resource> m_resources;
        std::vector<Pass> m_passes;
    };
} // namespace Fenrir::Render
