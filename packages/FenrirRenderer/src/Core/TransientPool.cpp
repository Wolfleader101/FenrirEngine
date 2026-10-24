#include "Core/TransientPool.hpp"

#include <algorithm>

namespace Fenrir::Render
{
    TransientPool::TransientPool(std::uint32_t framesInFlight) : m_sets(std::max<std::uint32_t>(framesInFlight, 1))
    {
    }

    void TransientPool::BeginFrame(std::uint64_t frameIndex)
    {
        m_current = static_cast<std::size_t>(frameIndex % m_sets.size());
        for (Entry& entry : CurrentSet())
        {
            entry.held = false;
            entry.usedThisFrame = false;
        }
    }

    std::vector<TransientPool::Entry>& TransientPool::CurrentSet()
    {
        return m_sets[m_current];
    }

    Graphics::TextureHandle TransientPool::Acquire(Graphics::GraphicsDevice& device, const Graphics::TextureDesc& desc)
    {
        for (Entry& entry : CurrentSet())
        {
            if (!entry.held && entry.desc == decltype(entry.desc){desc})
            {
                entry.held = true;
                entry.usedThisFrame = true;
                return entry.texture;
            }
        }
        Entry entry{desc, device.CreateTexture(desc, "transient"), {}, true, true};
        CurrentSet().push_back(entry);
        return entry.texture;
    }

    Graphics::BufferHandle TransientPool::Acquire(Graphics::GraphicsDevice& device, const Graphics::BufferDesc& desc)
    {
        for (Entry& entry : CurrentSet())
        {
            if (!entry.held && entry.desc == decltype(entry.desc){desc})
            {
                entry.held = true;
                entry.usedThisFrame = true;
                return entry.buffer;
            }
        }
        Entry entry{desc, {}, device.CreateBuffer(desc, {}, "transient"), true, true};
        CurrentSet().push_back(entry);
        return entry.buffer;
    }

    void TransientPool::Release(Graphics::TextureHandle texture)
    {
        for (Entry& entry : CurrentSet())
        {
            if (entry.texture == texture && !texture.IsEmpty())
            {
                entry.held = false;
                return;
            }
        }
    }

    void TransientPool::Release(Graphics::BufferHandle buffer)
    {
        for (Entry& entry : CurrentSet())
        {
            if (entry.buffer == buffer && !buffer.IsEmpty())
            {
                entry.held = false;
                return;
            }
        }
    }

    void TransientPool::EndFrame(Graphics::GraphicsDevice& device)
    {
        std::vector<Entry>& set = CurrentSet();
        const auto unused =
            std::stable_partition(set.begin(), set.end(), [](const Entry& e) { return e.usedThisFrame; });
        for (auto it = unused; it != set.end(); ++it)
        {
            if (!it->texture.IsEmpty())
            {
                device.Destroy(it->texture);
            }
            if (!it->buffer.IsEmpty())
            {
                device.Destroy(it->buffer);
            }
        }
        set.erase(unused, set.end());
    }

    void TransientPool::Clear(Graphics::GraphicsDevice& device)
    {
        for (std::vector<Entry>& set : m_sets)
        {
            for (const Entry& entry : set)
            {
                if (!entry.texture.IsEmpty())
                {
                    device.Destroy(entry.texture);
                }
                if (!entry.buffer.IsEmpty())
                {
                    device.Destroy(entry.buffer);
                }
            }
            set.clear();
        }
    }
} // namespace Fenrir::Render
