#include "Core/UniformBlocks.hpp"

#include <cstring>

namespace Fenrir::Render
{
    using namespace Fenrir::Graphics;

    BufferHandle MakeAlignedBlocks(GraphicsDevice& device, std::span<const std::span<const std::byte>> blocks,
                                   std::string_view label)
    {
        std::vector<std::byte> bytes(UniformBlockAlignment * blocks.size());
        for (std::size_t i = 0; i < blocks.size(); ++i)
        {
            std::memcpy(bytes.data() + UniformBlockAlignment * i, blocks[i].data(), blocks[i].size());
        }
        return device.CreateBuffer({bytes.size(), BufferUsage::Uniform}, bytes, label);
    }
} // namespace Fenrir::Render
