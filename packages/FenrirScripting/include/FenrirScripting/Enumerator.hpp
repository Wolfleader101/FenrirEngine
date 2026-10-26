#pragma once

// Whether an enum value C# passed names an enumerator. Natives take enums by value (ADR-0044), and C# can cast any
// integer to one, so a native that switches on one, or hands it to a package that does, checks it first and warns once
// when it names none. For enums whose enumerators run from zero to `Last` without gaps, which the engine's do.

#include <type_traits>

namespace Fenrir
{
    /** @brief Whether `value` is one of `Last`'s enum's enumerators, which run from zero to `Last` without gaps. */
    template <auto Last>
    [[nodiscard]] constexpr bool IsEnumerator(decltype(Last) value) noexcept
    {
        using Enum = decltype(Last);
        static_assert(std::is_enum_v<Enum>, "Last is an enumerator");
        using Underlying = std::underlying_type_t<Enum>;
        const Underlying raw = static_cast<Underlying>(value);
        if constexpr (std::is_signed_v<Underlying>)
        {
            if (raw < 0)
            {
                return false;
            }
        }
        return raw <= static_cast<Underlying>(Last);
    }
} // namespace Fenrir
