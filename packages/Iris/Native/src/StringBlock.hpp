#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Iris::Detail
{
    struct BridgeFunctions;

    // Copies a returned string block and gives it back to the bridge. A zero slot is a null string.
    // `bridge` is the table the block came from.
    std::optional<std::string> TakeString(const BridgeFunctions& bridge, std::uint64_t block);

    // Copies `size` bytes of a returned struct block into `out` and gives the block back.
    void TakeBytes(const BridgeFunctions& bridge, std::uint64_t block, void* out, std::size_t size) noexcept;
} // namespace Iris::Detail
