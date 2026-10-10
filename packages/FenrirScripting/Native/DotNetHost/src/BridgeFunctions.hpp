#pragma once

#include <array>
#include <cstdint>

namespace Fenrir::Scripting::Detail
{
#if defined(_WIN32)
#define FENRIR_BRIDGE_CALL __cdecl
#else
#define FENRIR_BRIDGE_CALL
#endif
    // The bridge ABI: a table of Cdecl function pointers filled by the managed
    // Bootstrap.GetBridgeFunctions. Field order, count and signatures must match the managed
    // BridgeFunctions struct; any change bumps BridgeVersion on both sides.
    // All pointer arguments are borrowed for one call. Tokens are uint64; flags, counts and statuses are int32.
    // The bridge's only public entry point: (expected version, table, table size, reported version,
    // reported size). Always reports; fills the table only when version and size match.
    using GetBridgeFunctionsFn = std::int32_t(FENRIR_BRIDGE_CALL*)(std::int32_t, void*, std::int32_t, std::int32_t*,
                                                                   std::int32_t*);

    struct BridgeFunctions
    {
        using TryAddInt32Fn = std::int32_t(FENRIR_BRIDGE_CALL*)(std::int32_t, std::int32_t, std::int32_t*,
                                                                std::uint8_t*, std::int32_t, std::int32_t*,
                                                                std::int32_t*);
        using CopyUtf8Fn = std::int32_t(FENRIR_BRIDGE_CALL*)(std::uint8_t*, std::int32_t, std::int32_t*, std::uint8_t*,
                                                             std::int32_t, std::int32_t*, std::int32_t*);
        using EchoUtf8Fn = std::int32_t(FENRIR_BRIDGE_CALL*)(const std::uint8_t*, std::int32_t, std::uint8_t*,
                                                             std::int32_t, std::int32_t*, std::uint8_t*, std::int32_t,
                                                             std::int32_t*, std::int32_t*);
        using LoadFn = std::int32_t(FENRIR_BRIDGE_CALL*)(const std::uint8_t*, std::int32_t, std::uint64_t*,
                                                         std::int32_t*, std::uint8_t*, std::int32_t, std::int32_t*,
                                                         std::int32_t*);
        using NameFn = std::int32_t(FENRIR_BRIDGE_CALL*)(std::uint64_t, std::uint8_t*, std::int32_t, std::int32_t*,
                                                         std::uint8_t*, std::int32_t, std::int32_t*, std::int32_t*);
        using TokenFn = std::int32_t(FENRIR_BRIDGE_CALL*)(std::uint64_t, std::uint8_t*, std::int32_t, std::int32_t*,
                                                          std::int32_t*);
        using InvokeFn = std::int32_t(FENRIR_BRIDGE_CALL*)(std::uint64_t, const std::uint8_t*, std::int32_t,
                                                           const std::uint8_t*, std::int32_t, std::int32_t,
                                                           std::int32_t, std::int32_t*, std::uint8_t*, std::int32_t,
                                                           std::int32_t*, std::int32_t*);
        using UnloadStateFn = std::int32_t(FENRIR_BRIDGE_CALL*)(std::uint64_t, std::int32_t*, std::uint8_t*,
                                                                std::int32_t, std::int32_t*, std::int32_t*);

        TryAddInt32Fn tryAddInt32;
        CopyUtf8Fn copyGreetingUtf8;
        EchoUtf8Fn echoUtf8;
        LoadFn load;
        NameFn copyName;
        TokenFn release;
        InvokeFn invokeInt32;
        UnloadStateFn getUnloadState;
        TokenFn forgetUnload;
    };
#undef FENRIR_BRIDGE_CALL

    inline constexpr std::int32_t BridgeVersion = 7;

    // Managed names of the table slots, in field order, for diagnostics naming a missing entry.
    inline constexpr std::array<const char*, 9> BridgeFunctionNames = {
        "TryAddInt32",          "CopyGreetingUtf8",        "EchoUtf8",
        "LoadComponent",        "CopyComponentNameUtf8",   "ReleaseComponent",
        "InvokeComponentInt32", "GetComponentUnloadState", "ForgetComponentUnload"};

    static_assert(sizeof(void (*)()) == sizeof(void*), "Bridge slots are copied as object pointers");
    static_assert(sizeof(BridgeFunctions) == BridgeFunctionNames.size() * sizeof(void*),
                  "Every bridge slot needs a name, and the table must have no padding");
} // namespace Fenrir::Scripting::Detail
