#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "Device/GraphicsDevice.hpp"

namespace Fenrir::Render
{
    /**
     * @brief A per-frame buffer (uniforms, debug vertices) ringed by frame index (ADR-0019): one device buffer per
     * frame in flight, so writing this frame's copy never touches one a frame still in flight reads. It destroys its
     * buffers when it is destroyed, so the device must outlive it.
     */
    class FrameRingBuffer
    {
      public:
        /** @brief Makes `device.FramesInFlight()` buffers of `desc`. */
        FrameRingBuffer(Graphics::GraphicsDevice& device, const Graphics::BufferDesc& desc,
                        std::string_view label = {});
        ~FrameRingBuffer();

        FrameRingBuffer(const FrameRingBuffer&) = delete;
        FrameRingBuffer& operator=(const FrameRingBuffer&) = delete;
        FrameRingBuffer(FrameRingBuffer&& other) noexcept;
        FrameRingBuffer& operator=(FrameRingBuffer&& other) noexcept;

        /** @brief The copy frame `frameIndex` uses. */
        [[nodiscard]] Graphics::BufferHandle Current(std::uint64_t frameIndex) const;

        /** @brief Writes `bytes` at `offset` into frame `frameIndex`'s copy. */
        bool Write(std::uint64_t frameIndex, std::size_t offset, std::span<const std::byte> bytes);

        /** @brief How many copies it rings over. */
        [[nodiscard]] std::size_t CopyCount() const noexcept
        {
            return m_buffers.size();
        }

      private:
        void DestroyBuffers();

        Graphics::GraphicsDevice* m_device = nullptr;
        std::vector<Graphics::BufferHandle> m_buffers;
    };
} // namespace Fenrir::Render
