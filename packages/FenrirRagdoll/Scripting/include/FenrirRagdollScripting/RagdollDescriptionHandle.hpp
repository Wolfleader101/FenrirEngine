#pragma once

// The handle C# holds for a ragdoll description: the description itself holds vectors and text, so it stays C++'s,
// kept by the extension for the scene it was described in, and C# names it by this one number (the ragdolls spec,
// "FenrirRagdollScripting and Fenrir.Ragdoll"). Described by this library's own selection
// (api/FenrirRagdollScripting.api.toml); a wrapper, so a native takes and returns it by value as its id (ADR-0044,
// RagdollMappings.hpp).

#include <cstdint>

namespace Fenrir
{
    /**
     * @brief A ragdoll description C# asked `Ragdoll.DescribeHumanoid` for, kept natively for the scene it was
     * described in until `Ragdoll.Build` takes it or the scene ends. The id 0 is no description (C#'s `None`), what a
     * refused description gives.
     */
    struct RagdollDescriptionHandle
    {
        /** @brief Which description: 0 for none. Ids are never reused while the extension lasts. */
        std::uint32_t id = 0;
    };
} // namespace Fenrir
