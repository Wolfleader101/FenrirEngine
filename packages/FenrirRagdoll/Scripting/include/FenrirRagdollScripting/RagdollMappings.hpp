#pragma once

// The description handle as a wrapper: a native function takes and returns a RagdollDescriptionHandle by value,
// crossing as its one 32-bit id (ADR-0044). Everything else C# sees of FenrirRagdoll is generated
// (Scripting/api/FenrirRagdoll.binding.toml) into Generated/FenrirRagdollMappings.hpp, which includes this header.

#include <Iris/InternalCall.hpp>

#include <cstdint>

#include "FenrirRagdollScripting/RagdollDescriptionHandle.hpp"

template <>
struct Iris::WrapperOf<Fenrir::RagdollDescriptionHandle>
{
    using Value = std::uint32_t;

    static Fenrir::RagdollDescriptionHandle From(Value value)
    {
        return Fenrir::RagdollDescriptionHandle{value};
    }

    static Value To(Fenrir::RagdollDescriptionHandle handle)
    {
        return handle.id;
    }
};
