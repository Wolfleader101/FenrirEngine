#include "Core/BlockRing.hpp"

#include <algorithm>
#include <utility>

namespace Fenrir::Render
{
    namespace
    {
        using namespace Fenrir::Graphics;

        /** @brief Uniform ranges are bound at multiples of this, the largest alignment GL implementations ask for. */
        constexpr std::size_t UniformAlignment = 256;

        std::size_t RoundUp(std::size_t size, std::size_t multiple)
        {
            return (size + multiple - 1) / multiple * multiple;
        }
    } // namespace

    BlockRing::BlockRing(GraphicsDevice& device, BindingGroupLayoutHandle layout, bool storage, std::size_t slotSize,
                         std::string label)
        : m_device(device), m_layout(layout), m_storage(storage),
          m_slotSize(storage ? slotSize : RoundUp(slotSize, UniformAlignment)), m_label(std::move(label))
    {
    }

    BlockRing::~BlockRing()
    {
        DestroyGroups();
    }

    void BlockRing::DestroyGroups()
    {
        for (const std::vector<BindingGroupHandle>& copy : m_groups)
        {
            for (const BindingGroupHandle group : copy)
            {
                m_device.Destroy(group);
            }
        }
        m_groups.clear();
    }

    void BlockRing::Reserve(std::size_t slots)
    {
        slots = std::max<std::size_t>(slots, 1);
        if (m_ring != nullptr && slots <= m_slots)
        {
            return;
        }
        // Grow by doubling, so a scene that keeps growing remakes its buffers rarely.
        m_slots = std::max(slots, m_slots * 2);
        DestroyGroups();
        m_ring.reset();
        const BufferUsage usage = m_storage ? BufferUsage::Storage : BufferUsage::Uniform;
        m_ring = std::make_unique<FrameRingBuffer>(m_device, BufferDesc{m_slots * m_slotSize, usage}, m_label);
        for (std::size_t copy = 0; copy < m_ring->CopyCount(); ++copy)
        {
            const BufferHandle buffer = m_ring->Current(copy);
            std::vector<BindingGroupHandle>& groups = m_groups.emplace_back();
            const std::size_t groupCount = m_storage ? 1 : m_slots;
            for (std::size_t slot = 0; slot < groupCount; ++slot)
            {
                BindingGroupEntry entry;
                entry.binding = 0;
                entry.buffer = buffer;
                entry.offset = m_storage ? 0 : slot * m_slotSize;
                entry.size = m_storage ? 0 : m_slotSize;
                groups.push_back(m_device.CreateBindingGroup({m_layout, {entry}}, m_label));
            }
        }
    }

    void BlockRing::Write(std::uint64_t frameIndex, std::size_t slot, std::span<const std::byte> bytes)
    {
        m_ring->Write(frameIndex, slot * m_slotSize, bytes);
    }

    BindingGroupHandle BlockRing::Group(std::uint64_t frameIndex, std::size_t slot) const
    {
        const std::vector<BindingGroupHandle>& copy =
            m_groups.at(static_cast<std::size_t>(frameIndex % m_groups.size()));
        return copy.at(m_storage ? 0 : slot);
    }
} // namespace Fenrir::Render
