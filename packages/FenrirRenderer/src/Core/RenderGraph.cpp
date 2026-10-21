#include "RenderGraph.hpp"

#include <algorithm>
#include <numeric>

namespace Fenrir::Render
{
    Gpu::TextureHandle TransientPool::Acquire(Gpu::GraphicsDevice& device, const TransientDesc& desc,
                                              std::uint64_t frame)
    {
        for (Entry& e : m_entries)
        {
            if (!e.inUse && e.desc == desc)
            {
                e.inUse = true;
                e.lastUsed = frame;
                return e.texture;
            }
        }
        Gpu::TextureDesc td;
        td.format = desc.format;
        td.width = desc.width;
        td.height = desc.height;
        td.renderTarget = true;
        td.label = "transient";
        m_entries.push_back({desc, device.CreateTexture(td), frame, true});
        return m_entries.back().texture;
    }

    void TransientPool::EndFrame(Gpu::GraphicsDevice& device, std::uint64_t frame)
    {
        for (auto it = m_entries.begin(); it != m_entries.end();)
        {
            it->inUse = false;
            if (it->lastUsed + 3 < frame) // unused for a few frames (a resize): deferred destruction frees it
            {
                device.Destroy(it->texture);
                it = m_entries.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    GraphTexture RenderGraph::ImportBackbuffer(int width, int height)
    {
        m_resources.push_back({"Backbuffer", {Gpu::TextureFormat::Rgba8Srgb, width, height}, true});
        return {static_cast<std::uint32_t>(m_resources.size() - 1)};
    }

    GraphTexture RenderGraph::CreateTransient(std::string name, TransientDesc desc)
    {
        m_resources.push_back({std::move(name), desc, false});
        return {static_cast<std::uint32_t>(m_resources.size() - 1)};
    }

    void RenderGraph::AddPass(std::string name, RenderStage stage, const std::function<void(PassBuilder&)>& setup,
                              std::function<void(PassContext&)> record)
    {
        m_passes.push_back({std::move(name), stage, {}, std::move(record)});
        PassBuilder builder(m_passes.back());
        setup(builder);
    }

    RenderGraph::Result RenderGraph::Execute(Gpu::GraphicsDevice& device, TransientPool& pool, std::uint64_t frame)
    {
        Result result;
        std::vector<std::size_t> order(m_passes.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::stable_sort(order.begin(), order.end(),
                         [this](std::size_t a, std::size_t b) { return m_passes[a].stage < m_passes[b].stage; });

        // Cull: walking back from the imported outputs, a pass lives if it writes something still needed.
        std::vector<bool> needed(m_resources.size(), false);
        for (std::size_t r = 0; r < m_resources.size(); ++r)
            needed[r] = m_resources[r].imported;
        std::vector<bool> live(m_passes.size(), false);
        for (auto it = order.rbegin(); it != order.rend(); ++it)
        {
            const Pass& pass = m_passes[*it];
            for (const Access& a : pass.accesses)
                if (a.write && needed[a.resource])
                    live[*it] = true;
            if (live[*it])
                for (const Access& a : pass.accesses)
                    if (!a.write)
                        needed[a.resource] = true;
        }

        // Validate: a live pass reading what no earlier live pass wrote.
        std::vector<bool> written(m_resources.size(), false);
        for (std::size_t r = 0; r < m_resources.size(); ++r)
            written[r] = m_resources[r].imported;
        for (std::size_t index : order)
        {
            if (!live[index])
            {
                ++result.passesCulled;
                continue;
            }
            for (const Access& a : m_passes[index].accesses)
            {
                if (!a.write && !written[a.resource])
                    result.errors.push_back(m_passes[index].name + " reads " + m_resources[a.resource].name +
                                            ", which nothing wrote");
            }
            for (const Access& a : m_passes[index].accesses)
                if (a.write)
                    written[a.resource] = true;
        }

        // Transients, from first use. Nothing aliases (GL could not use it); the pool reuses by description.
        std::vector<Gpu::TextureHandle> textures(m_resources.size());
        auto resolve = [&](GraphTexture t) -> Gpu::TextureHandle {
            if (t.id >= m_resources.size() || m_resources[t.id].imported)
                return {};
            if (!textures[t.id].IsValid())
                textures[t.id] = pool.Acquire(device, m_resources[t.id].desc, frame);
            return textures[t.id];
        };

        std::vector<Gpu::CommandList> lists;
        lists.reserve(m_passes.size());
        for (std::size_t index : order)
        {
            if (!live[index])
                continue;
            lists.emplace_back();
            PassContext context{lists.back(), resolve};
            m_passes[index].record(context);
            result.order.push_back(m_passes[index].name);
            ++result.passesRun;
        }
        for (const Gpu::CommandList& list : lists)
            device.Submit(list);
        return result;
    }
} // namespace Fenrir::Render
