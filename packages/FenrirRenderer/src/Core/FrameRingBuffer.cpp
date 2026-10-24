#include "Core/FrameRingBuffer.hpp"

#include <utility>

namespace Fenrir::Render
{
    FrameRingBuffer::FrameRingBuffer(Graphics::GraphicsDevice& device, const Graphics::BufferDesc& desc,
                                     std::string_view label)
        : m_device(&device)
    {
        const std::uint32_t copies = device.FramesInFlight() == 0 ? 1 : device.FramesInFlight();
        m_buffers.reserve(copies);
        for (std::uint32_t i = 0; i < copies; ++i)
        {
            m_buffers.push_back(device.CreateBuffer(desc, {}, label));
        }
    }

    FrameRingBuffer::~FrameRingBuffer()
    {
        DestroyBuffers();
    }

    FrameRingBuffer::FrameRingBuffer(FrameRingBuffer&& other) noexcept
        : m_device(std::exchange(other.m_device, nullptr)), m_buffers(std::move(other.m_buffers))
    {
        other.m_buffers.clear();
    }

    FrameRingBuffer& FrameRingBuffer::operator=(FrameRingBuffer&& other) noexcept
    {
        if (this != &other)
        {
            DestroyBuffers();
            m_device = std::exchange(other.m_device, nullptr);
            m_buffers = std::move(other.m_buffers);
            other.m_buffers.clear();
        }
        return *this;
    }

    void FrameRingBuffer::DestroyBuffers()
    {
        if (m_device != nullptr)
        {
            for (Graphics::BufferHandle buffer : m_buffers)
            {
                m_device->Destroy(buffer);
            }
        }
        m_buffers.clear();
    }

    Graphics::BufferHandle FrameRingBuffer::Current(std::uint64_t frameIndex) const
    {
        if (m_buffers.empty())
        {
            return {};
        }
        return m_buffers[static_cast<std::size_t>(frameIndex % m_buffers.size())];
    }

    bool FrameRingBuffer::Write(std::uint64_t frameIndex, std::size_t offset, std::span<const std::byte> bytes)
    {
        return m_device != nullptr && m_device->WriteBuffer(Current(frameIndex), offset, bytes);
    }
} // namespace Fenrir::Render
