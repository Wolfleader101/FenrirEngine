#include "Core/RenderGraph.hpp"

#include <algorithm>
#include <functional>
#include <numeric>
#include <queue>
#include <utility>

#include "Core/TransientPool.hpp"

namespace Fenrir::Render
{
    using Graphics::LoadOp;
    using Graphics::ResourceUsage;

    namespace
    {
        Graphics::TextureUsage TextureUsageOf(ResourceUsage usage)
        {
            // Indexed by ResourceUsage, in its order.
            constexpr std::array<Graphics::TextureUsage, 8> byUsage{
                Graphics::TextureUsage::ColourAttachment, Graphics::TextureUsage::DepthAttachment,
                Graphics::TextureUsage::DepthAttachment,  Graphics::TextureUsage::Sampled,
                Graphics::TextureUsage::Storage,          Graphics::TextureUsage::Storage,
                Graphics::TextureUsage::CopySource,       Graphics::TextureUsage::CopyDestination,
            };
            return byUsage.at(static_cast<std::size_t>(usage));
        }

        Graphics::BufferUsage BufferUsageOf(ResourceUsage usage)
        {
            switch (usage)
            {
            case ResourceUsage::StorageRead:
            case ResourceUsage::StorageWrite:
                return Graphics::BufferUsage::Storage;
            case ResourceUsage::CopySource:
                return Graphics::BufferUsage::CopySource;
            case ResourceUsage::CopyDestination:
                return Graphics::BufferUsage::CopyDestination;
            default:
                return Graphics::BufferUsage::None;
            }
        }
    } // namespace

    void PassBuilder::Use(GraphResource resource, ResourceUsage usage, bool reads, bool writes)
    {
        RenderGraph::Access access;
        access.resource = resource.id;
        access.usage = usage;
        access.reads = reads;
        access.writes = writes;
        m_graph.m_passes[m_pass].accesses.push_back(access);
    }

    void PassBuilder::ColourAttachment(GraphResource texture, LoadOp load, std::array<float, 4> clear)
    {
        Use(texture, ResourceUsage::ColourAttachment, load == LoadOp::Load, true);
        RenderGraph::Access& access = m_graph.m_passes[m_pass].accesses.back();
        access.load = load;
        access.clearColour = clear;
    }

    void PassBuilder::DepthAttachment(GraphResource texture, LoadOp load, float clear)
    {
        Use(texture, ResourceUsage::DepthWrite, load == LoadOp::Load, true);
        RenderGraph::Access& access = m_graph.m_passes[m_pass].accesses.back();
        access.load = load;
        access.clearDepth = clear;
    }

    void PassBuilder::DepthReadOnly(GraphResource texture)
    {
        Use(texture, ResourceUsage::DepthRead, true, false);
        m_graph.m_passes[m_pass].accesses.back().load = LoadOp::Load;
    }

    void PassBuilder::Sample(GraphResource texture)
    {
        Use(texture, ResourceUsage::Sampled, true, false);
    }

    void PassBuilder::StorageRead(GraphResource resource)
    {
        Use(resource, ResourceUsage::StorageRead, true, false);
    }

    void PassBuilder::StorageWrite(GraphResource resource)
    {
        Use(resource, ResourceUsage::StorageWrite, false, true);
    }

    void PassBuilder::CopySource(GraphResource resource)
    {
        Use(resource, ResourceUsage::CopySource, true, false);
    }

    void PassBuilder::CopyDestination(GraphResource resource)
    {
        Use(resource, ResourceUsage::CopyDestination, false, true);
    }

    Graphics::TextureHandle PassContext::Texture(GraphResource resource) const
    {
        return resource.id < m_textures.size() ? m_textures[resource.id] : Graphics::TextureHandle{};
    }

    Graphics::BufferHandle PassContext::Buffer(GraphResource resource) const
    {
        return resource.id < m_buffers.size() ? m_buffers[resource.id] : Graphics::BufferHandle{};
    }

    GraphResource RenderGraph::ImportTexture(std::string name, Graphics::TextureHandle texture,
                                             const Graphics::TextureDesc& desc)
    {
        Resource resource;
        resource.name = std::move(name);
        resource.desc = desc;
        resource.importedTexture = texture;
        resource.imported = true;
        m_resources.push_back(std::move(resource));
        return {static_cast<std::uint32_t>(m_resources.size() - 1)};
    }

    GraphResource RenderGraph::ImportBuffer(std::string name, Graphics::BufferHandle buffer,
                                            const Graphics::BufferDesc& desc)
    {
        Resource resource;
        resource.name = std::move(name);
        resource.desc = desc;
        resource.importedBuffer = buffer;
        resource.imported = true;
        m_resources.push_back(std::move(resource));
        return {static_cast<std::uint32_t>(m_resources.size() - 1)};
    }

    GraphResource RenderGraph::CreateTexture(std::string name, const Graphics::TextureDesc& desc)
    {
        Resource resource;
        resource.name = std::move(name);
        resource.desc = desc;
        m_resources.push_back(std::move(resource));
        return {static_cast<std::uint32_t>(m_resources.size() - 1)};
    }

    GraphResource RenderGraph::CreateBuffer(std::string name, const Graphics::BufferDesc& desc)
    {
        Resource resource;
        resource.name = std::move(name);
        resource.desc = desc;
        m_resources.push_back(std::move(resource));
        return {static_cast<std::uint32_t>(m_resources.size() - 1)};
    }

    void RenderGraph::AddPass(std::string name, RenderStage stage, Graphics::QueueKind queue,
                              const std::function<void(PassBuilder&)>& setup, std::function<void(PassContext&)> record)
    {
        Pass pass;
        pass.name = std::move(name);
        pass.stage = stage;
        pass.queue = queue;
        pass.record = std::move(record);
        m_passes.push_back(std::move(pass));
        PassBuilder builder(*this, static_cast<std::uint32_t>(m_passes.size() - 1));
        if (setup)
        {
            setup(builder);
        }
    }

    const std::string& RenderGraph::PassName(std::uint32_t pass) const
    {
        return m_passes[pass].name;
    }

    std::vector<std::uint32_t> RenderGraph::OrderPasses(std::vector<std::string>& errors) const
    {
        const std::size_t passCount = m_passes.size();

        // The order with no dependencies: by stage, then registration.
        std::vector<std::uint32_t> baseline(passCount);
        std::iota(baseline.begin(), baseline.end(), 0u);
        std::stable_sort(baseline.begin(), baseline.end(),
                         [this](std::uint32_t a, std::uint32_t b) { return m_passes[a].stage < m_passes[b].stage; });
        std::vector<std::size_t> rank(passCount);
        for (std::size_t i = 0; i < passCount; ++i)
        {
            rank[baseline[i]] = i;
        }

        // Each resource's users in baseline order, one entry per pass. Reads before any write in that order read the
        // finished resource instead, so they move after its last user: a view sampling an image another view draws in
        // a later stage runs after it.
        struct User
        {
            std::uint32_t pass = 0;
            bool reads = false;
            bool writes = false;
        };
        std::vector<std::vector<User>> users(m_resources.size());
        for (std::uint32_t pass : baseline)
        {
            for (const Access& access : m_passes[pass].accesses)
            {
                std::vector<User>& list = users[access.resource];
                if (list.empty() || list.back().pass != pass)
                {
                    list.push_back({pass, false, false});
                }
                list.back().reads = list.back().reads || access.reads;
                list.back().writes = list.back().writes || access.writes;
            }
        }

        std::vector<std::vector<std::uint32_t>> after(passCount);
        std::vector<std::size_t> waitingOn(passCount, 0);
        const auto addEdge = [&](std::uint32_t from, std::uint32_t to) {
            if (from != to)
            {
                after[from].push_back(to);
                ++waitingOn[to];
            }
        };
        for (std::vector<User>& list : users)
        {
            const auto firstWriter = std::find_if(list.begin(), list.end(), [](const User& u) { return u.writes; });
            if (firstWriter == list.end())
            {
                continue;
            }
            std::rotate(list.begin(), firstWriter, list.end());

            std::optional<std::uint32_t> lastWriter;
            std::vector<std::uint32_t> readersSinceWrite;
            for (const User& user : list)
            {
                if (lastWriter)
                {
                    addEdge(*lastWriter, user.pass);
                }
                if (user.writes)
                {
                    for (std::uint32_t reader : readersSinceWrite)
                    {
                        addEdge(reader, user.pass);
                    }
                    readersSinceWrite.clear();
                    lastWriter = user.pass;
                }
                else
                {
                    readersSinceWrite.push_back(user.pass);
                }
            }
        }

        // Kahn's algorithm, always taking the ready pass earliest in the baseline order.
        using Ready = std::pair<std::size_t, std::uint32_t>;
        std::priority_queue<Ready, std::vector<Ready>, std::greater<>> ready;
        for (std::uint32_t pass = 0; pass < passCount; ++pass)
        {
            if (waitingOn[pass] == 0)
            {
                ready.emplace(rank[pass], pass);
            }
        }
        std::vector<std::uint32_t> order;
        order.reserve(passCount);
        while (!ready.empty())
        {
            const std::uint32_t pass = ready.top().second;
            ready.pop();
            order.push_back(pass);
            for (std::uint32_t next : after[pass])
            {
                if (--waitingOn[next] == 0)
                {
                    ready.emplace(rank[next], next);
                }
            }
        }
        if (order.size() != passCount)
        {
            std::string names;
            for (std::uint32_t pass = 0; pass < passCount; ++pass)
            {
                if (waitingOn[pass] != 0)
                {
                    names += (names.empty() ? "" : ", ") + m_passes[pass].name;
                }
            }
            errors.push_back("render passes " + names + " read each other's writes in a cycle");
            return baseline;
        }
        return order;
    }

    GraphCompilation RenderGraph::Compile() const
    {
        GraphCompilation compilation;

        std::vector<bool> written(m_resources.size(), false);
        for (const Pass& pass : m_passes)
        {
            for (const Access& access : pass.accesses)
            {
                if (access.resource >= m_resources.size())
                {
                    compilation.errors.push_back("render pass " + pass.name + " uses a resource of another graph");
                    return compilation;
                }
                written[access.resource] = written[access.resource] || access.writes;
            }
        }
        for (const Pass& pass : m_passes)
        {
            for (const Access& access : pass.accesses)
            {
                const Resource& resource = m_resources[access.resource];
                if (access.reads && !written[access.resource] && !resource.imported)
                {
                    compilation.errors.push_back("render pass " + pass.name + " reads " + resource.name +
                                                 ", which nothing wrote");
                }
            }
        }

        const std::vector<std::uint32_t> order = OrderPasses(compilation.errors);
        if (!compilation.Ok())
        {
            return compilation;
        }

        // Cull, walking back from the end: a pass runs if it writes something a later running pass reads, or an
        // imported resource, which is read outside the graph. A transient's write that is not also a read hides earlier
        // writes; an imported resource's never does, since passes may each draw a part of it (a view's viewport).
        std::vector<bool> needed(m_resources.size(), false);
        for (std::size_t r = 0; r < m_resources.size(); ++r)
        {
            needed[r] = m_resources[r].imported;
        }
        std::vector<bool> runs(m_passes.size(), false);
        for (auto it = order.rbegin(); it != order.rend(); ++it)
        {
            const Pass& pass = m_passes[*it];
            const bool live = std::any_of(pass.accesses.begin(), pass.accesses.end(),
                                          [&needed](const Access& a) { return a.writes && needed[a.resource]; });
            if (!live)
            {
                continue;
            }
            runs[*it] = true;
            for (const Access& access : pass.accesses)
            {
                if (access.writes && !m_resources[access.resource].imported)
                {
                    needed[access.resource] = false;
                }
            }
            for (const Access& access : pass.accesses)
            {
                if (access.reads)
                {
                    needed[access.resource] = true;
                }
            }
        }

        compilation.m_lifetimes.resize(m_resources.size());
        for (std::uint32_t pass : order)
        {
            if (!runs[pass])
            {
                compilation.culled.push_back(pass);
                continue;
            }
            const std::size_t position = compilation.order.size();
            compilation.order.push_back(pass);
            for (const Access& access : m_passes[pass].accesses)
            {
                GraphCompilation::Lifetime& lifetime = compilation.m_lifetimes[access.resource];
                if (!lifetime.used)
                {
                    lifetime.first = position;
                    lifetime.used = true;
                }
                lifetime.last = position;
            }
        }
        return compilation;
    }

    Graphics::PassDesc RenderGraph::MakePassDesc(const Pass& pass, const std::vector<Graphics::TextureHandle>& textures,
                                                 const std::vector<Graphics::BufferHandle>& buffers) const
    {
        Graphics::PassDesc desc;
        desc.label = pass.name;
        desc.queue = pass.queue;
        for (const Access& access : pass.accesses)
        {
            const Graphics::TextureHandle texture = textures[access.resource];
            if (access.usage == ResourceUsage::ColourAttachment)
            {
                desc.colour.push_back({texture, access.load, access.clearColour});
            }
            else if (access.usage == ResourceUsage::DepthWrite || access.usage == ResourceUsage::DepthRead)
            {
                desc.depth = Graphics::DepthAttachment{texture, access.load, access.clearDepth,
                                                       access.usage == ResourceUsage::DepthRead};
            }
            desc.uses.push_back({texture, buffers[access.resource], access.usage});
        }
        return desc;
    }

    std::vector<RecordedPass> RenderGraph::Record(const GraphCompilation& compilation, Graphics::GraphicsDevice& device,
                                                  TransientPool& pool, std::uint64_t frameIndex)
    {
        std::vector<RecordedPass> recorded;
        if (!compilation.Ok())
        {
            return recorded;
        }

        // The usage flags each transient needs: what its creator gave, plus every declared use.
        std::vector<std::variant<Graphics::TextureDesc, Graphics::BufferDesc>> descs;
        descs.reserve(m_resources.size());
        for (const Resource& resource : m_resources)
        {
            descs.push_back(resource.desc);
        }
        for (const Pass& pass : m_passes)
        {
            for (const Access& access : pass.accesses)
            {
                auto& desc = descs[access.resource];
                if (auto* texture = std::get_if<Graphics::TextureDesc>(&desc))
                {
                    texture->usage = texture->usage | TextureUsageOf(access.usage);
                }
                else
                {
                    auto& buffer = std::get<Graphics::BufferDesc>(desc);
                    buffer.usage = buffer.usage | BufferUsageOf(access.usage);
                }
            }
        }

        std::vector<Graphics::TextureHandle> textures(m_resources.size());
        std::vector<Graphics::BufferHandle> buffers(m_resources.size());
        for (std::size_t r = 0; r < m_resources.size(); ++r)
        {
            textures[r] = m_resources[r].importedTexture;
            buffers[r] = m_resources[r].importedBuffer;
        }

        recorded.reserve(compilation.order.size());
        for (std::size_t position = 0; position < compilation.order.size(); ++position)
        {
            for (std::size_t r = 0; r < m_resources.size(); ++r)
            {
                const GraphCompilation::Lifetime& lifetime = compilation.m_lifetimes[r];
                if (!m_resources[r].imported && lifetime.used && lifetime.first == position)
                {
                    if (const auto* texture = std::get_if<Graphics::TextureDesc>(&descs[r]))
                    {
                        textures[r] = pool.Acquire(device, *texture);
                    }
                    else
                    {
                        buffers[r] = pool.Acquire(device, std::get<Graphics::BufferDesc>(descs[r]));
                    }
                }
            }

            Pass& pass = m_passes[compilation.order[position]];
            RecordedPass& out = recorded.emplace_back();
            out.name = pass.name;
            out.stage = pass.stage;
            {
                Graphics::PassEncoder encoder(out.commands, MakePassDesc(pass, textures, buffers));
                PassContext context(encoder, textures, buffers, frameIndex);
                if (pass.record)
                {
                    pass.record(context);
                }
            }

            for (std::size_t r = 0; r < m_resources.size(); ++r)
            {
                const GraphCompilation::Lifetime& lifetime = compilation.m_lifetimes[r];
                if (!m_resources[r].imported && lifetime.used && lifetime.last == position)
                {
                    pool.Release(textures[r]);
                    pool.Release(buffers[r]);
                }
            }
        }
        return recorded;
    }
} // namespace Fenrir::Render
