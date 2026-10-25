#pragma once

// How a handle crosses a native function by value (ADR-0044): packed into one 64-bit number, the generation in the
// high half and the index in the low, C#'s AssetHandle<T>.Bits and FromBits (Managed/Fenrir.Assets/Handles.cs). The one
// C++ spelling of that packing, for this extension's natives and for another package's that takes a handle
// (FenrirRenderer's SpawnModel). Plain C++ only: a natives source includes it, and describe-api reads those.

#include <cstdint>

#include "FenrirAssets/AssetHandle.hpp"

namespace Fenrir
{
    /** @brief `handle` as it crosses to and from C#: one number, the generation high and the index low. */
    template <typename T>
    [[nodiscard]] constexpr std::uint64_t AssetHandleBits(AssetHandle<T> handle) noexcept
    {
        return (std::uint64_t{handle.generation} << 32u) | handle.index;
    }

    /** @brief The handle `bits` packs (AssetHandleBits). */
    template <typename T>
    [[nodiscard]] constexpr AssetHandle<T> AssetHandleFromBits(std::uint64_t bits) noexcept
    {
        return {static_cast<std::uint32_t>(bits), static_cast<std::uint32_t>(bits >> 32u)};
    }
} // namespace Fenrir
