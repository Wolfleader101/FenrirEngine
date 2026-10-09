#pragma once

#include <cstdint>

namespace Fenrir::Scripting::Detail
{
#if defined(_WIN32)
#define FENRIR_COMPONENT_CALL __cdecl
#else
#define FENRIR_COMPONENT_CALL
#endif
    // All pointers are borrowed; token is uint64, flags/counts/statuses are int32.
    struct ComponentBridge
    {
        using LoadFn = std::int32_t(FENRIR_COMPONENT_CALL*)(const std::uint8_t*, std::int32_t, std::uint64_t*,
                                                            std::int32_t*, std::uint8_t*, std::int32_t, std::int32_t*,
                                                            std::int32_t*);
        using NameFn = std::int32_t(FENRIR_COMPONENT_CALL*)(std::uint64_t, std::uint8_t*, std::int32_t, std::int32_t*,
                                                            std::uint8_t*, std::int32_t, std::int32_t*, std::int32_t*);
        using ReleaseFn = std::int32_t(FENRIR_COMPONENT_CALL*)(std::uint64_t, std::uint8_t*, std::int32_t,
                                                               std::int32_t*, std::int32_t*);
        LoadFn load = nullptr;
        NameFn copyName = nullptr;
        ReleaseFn release = nullptr;
    };
#undef FENRIR_COMPONENT_CALL
} // namespace Fenrir::Scripting::Detail
