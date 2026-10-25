#pragma once

// A C# Entity as a binding's native functions take, return and hold it (ADR-0044): the entity's 32-bit value alone,
// since a Fenrir::Entity also holds a pointer to its list. One type for every binding, so that each describes it as a
// bound type of its own selection (named Entity in its [bound-types], crossing by value as its field through
// EntityValueMapping.hpp) and none spells it a second way. Plain data only: describe-api reads this header.

#include <cstdint>

namespace Fenrir
{
    /**
     * @brief A C# Entity: an entity's 32-bit value alone. The wrapper a native takes or returns where C# passes an
     * Entity, and the field a shared struct holds where C# holds one.
     */
    struct EntityValue
    {
        EntityValue() = default;

        // Named after the field it sets (the leading underscore is ignored), as an API description needs.
        constexpr explicit EntityValue(std::uint32_t _value) noexcept : value(_value)
        {
        }

        [[nodiscard]] friend constexpr bool operator==(EntityValue, EntityValue) noexcept = default;

        /** @brief The entity's value; Entity::Null (every bit set, C#'s Entity.None) names none. */
        std::uint32_t value = 0;
    };
} // namespace Fenrir
