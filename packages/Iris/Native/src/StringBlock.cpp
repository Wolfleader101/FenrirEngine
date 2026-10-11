#include "BridgeFunctions.hpp"
#include "Iris/Method.hpp"

#include <cstring>

namespace Iris
{
    // A block is an eight-byte length followed by that many UTF-8 bytes, allocated by the bridge.
    std::optional<std::string> Detail::TakeString(const BridgeFunctions& bridge, std::uint64_t block)
    {
        if (block == 0)
        {
            return std::nullopt;
        }
        const auto* memory = reinterpret_cast<const char*>(static_cast<std::uintptr_t>(block));
        std::int64_t length = 0;
        std::memcpy(&length, memory, sizeof(length));
        std::optional<std::string> text;
        try
        {
            text.emplace(memory + sizeof(length), static_cast<std::size_t>(length));
        }
        catch (...)
        {
            bridge.freeString(block);
            throw;
        }
        bridge.freeString(block);
        return text;
    }
} // namespace Iris
