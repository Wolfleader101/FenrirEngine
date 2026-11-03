#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#include "Device/GraphicsDevice.hpp"

namespace Fenrir::Render
{
    /** @brief Uniform blocks are bound at multiples of this, the largest offset alignment GL implementations want. */
    inline constexpr std::size_t UniformBlockAlignment = 256;

    /**
     * @brief A uniform buffer, labelled `label`, holding `blocks` one after another at multiples of
     * UniformBlockAlignment: block `i` at `UniformBlockAlignment * i`. Each block is at most that many bytes.
     */
    [[nodiscard]] Graphics::BufferHandle MakeAlignedBlocks(Graphics::GraphicsDevice& device,
                                                           std::span<const std::span<const std::byte>> blocks,
                                                           std::string_view label);

    /** @brief Each of `blocks` as its bytes, for MakeAlignedBlocks; valid while `blocks` is. */
    template <typename Block>
    [[nodiscard]] std::vector<std::span<const std::byte>> BlocksOf(std::span<const Block> blocks)
    {
        std::vector<std::span<const std::byte>> bytes;
        bytes.reserve(blocks.size());
        for (const Block& block : blocks)
        {
            bytes.push_back(std::as_bytes(std::span(&block, 1)));
        }
        return bytes;
    }
} // namespace Fenrir::Render
