#pragma once

#include "FenrirTypes/TypeDescription.hpp"

#include <concepts>

namespace Fenrir
{
    /**
     * @brief The compile-time door to a type's description: `Describe<T>::Description()` is the description of `T`
     * that the registration of `T`'s package adds to the type registry, the same object.
     *
     * A package's generated header specialises it for each type the package describes, so code that has the C++ type
     * reaches its fields without a type registry (ADR-0084). A generated specialisation also names the type's role as
     * `static constexpr Role role`, the description's `role`, so a template constrains on it at compile time. It has no
     * definition for a type nothing describes.
     */
    template <typename T>
    struct Describe;

    /** @brief A type whose description `Describe<T>` yields. */
    template <typename T>
    concept Described = requires {
        { Describe<T>::Description() } -> std::same_as<const TypeDescription&>;
    };
} // namespace Fenrir
