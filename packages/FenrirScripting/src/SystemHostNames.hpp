#pragma once

#include <string_view>

namespace Fenrir::Detail::SystemHostNames
{
    // The contract between the engine and the generated system host: the one place in C++ that spells these names.
    // The source generator (Managed/Fenrir.Scripting.Generators/SystemGenerator.cs) mirrors them when it writes the
    // class; a script whose host does not match them fails to start, which is how drift shows.

    /** @brief The full name of the class the generator writes, one per script assembly. */
    constexpr std::string_view Type = "Fenrir.Scripting.Generated.SystemHost";

    /** @brief `int PhaseMask()`: bit n is set when the script has a system in phase n. */
    constexpr std::string_view PhaseMask = "PhaseMask";

    /** @brief `void OnCreate()`: every system's OnCreate, once the generation is loaded. */
    constexpr std::string_view OnCreate = "OnCreate";

    /** @brief `void Run(int phase)`: the systems of one phase, in order. */
    constexpr std::string_view Run = "Run";

    /** @brief `void OnDestroy()`: every system's OnDestroy, before the generation is released. */
    constexpr std::string_view OnDestroy = "OnDestroy";
} // namespace Fenrir::Detail::SystemHostNames
