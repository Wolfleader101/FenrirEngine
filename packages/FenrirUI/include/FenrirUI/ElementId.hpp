#pragma once

#include <cstdint>
#include <limits>

namespace Fenrir
{
    /**
     * @brief An element id (ADR-0066): an index and a version naming one element of a UI root. Removing the element
     * makes it stale, and a slot used again gets a new version, so a stale id never names the new element. It is not an
     * entity. Default constructed, it names nothing.
     */
    struct ElementId
    {
        /** @brief The element's slot in its root. */
        std::uint32_t index = std::numeric_limits<std::uint32_t>::max();
        /** @brief The slot's version when the element was made. */
        std::uint32_t version = 0;

        /** @brief Whether it was made by a root at all; a stale id is still valid in this sense. */
        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return index != std::numeric_limits<std::uint32_t>::max();
        }

        friend constexpr bool operator==(ElementId, ElementId) noexcept = default;
    };
} // namespace Fenrir
