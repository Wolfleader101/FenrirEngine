#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "Core/FrameRingBuffer.hpp"
#include "Device/GraphicsDevice.hpp"

namespace Fenrir::Render
{
    /**
     * @brief Uniform or storage blocks written each frame, in one ring buffer per frame in flight (ADR-0019), with a
     * binding group per copy and slot. It grows as more slots are needed. Uniform slots are bound at multiples of 256
     * bytes, the largest alignment GL implementations ask for.
     */
    class BlockRing
    {
      public:
        /**
         * @brief Blocks of `slotSize` bytes bound at binding 0 of groups of `layout`: one group per slot for uniform
         * blocks, or, with `storage`, one storage block per copy spanning every slot.
         */
        BlockRing(Graphics::GraphicsDevice& device, Graphics::BindingGroupLayoutHandle layout, bool storage,
                  std::size_t slotSize, std::string label);
        ~BlockRing();
        BlockRing(const BlockRing&) = delete;
        BlockRing& operator=(const BlockRing&) = delete;
        BlockRing(BlockRing&&) = delete;
        BlockRing& operator=(BlockRing&&) = delete;

        /** @brief Makes room for `slots` slots, remaking the buffers if there is not. */
        void Reserve(std::size_t slots);
        /** @brief Writes `bytes` at slot `slot` of frame `frameIndex`'s copy; a storage ring's may span several slots.
         */
        void Write(std::uint64_t frameIndex, std::size_t slot, std::span<const std::byte> bytes);
        /**
         * @brief The binding group of slot `slot` in frame `frameIndex`'s copy; a storage ring has one group per copy
         * spanning every slot.
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
} // namespace Fenrir::Render
