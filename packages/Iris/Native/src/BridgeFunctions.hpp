#pragma once

#include <array>
#include <cstdint>

namespace Iris::Detail
{
#if defined(_WIN32)
#define IRIS_BRIDGE_CALL __cdecl
#else
#define IRIS_BRIDGE_CALL
#endif
    // The bridge's only public entry point: (expected version, table, table size, reported version,
    // reported size). Always reports; fills the table only when version and size match.
    using GetBridgeFunctionsFn = std::int32_t(IRIS_BRIDGE_CALL*)(std::int32_t, void*, std::int32_t, std::int32_t*,
                                                                 std::int32_t*);

    // The bridge ABI: a table of Cdecl function pointers filled by the managed
    // Bootstrap.GetBridgeFunctions. Field order, count and signatures must match the managed
    // BridgeFunctions struct; any change bumps BridgeVersion on both sides.
    // All pointer arguments are borrowed for one call. Tokens are uint64; flags, counts and statuses are int32.
    struct BridgeFunctions
    {
        using LoadFn = std::int32_t(IRIS_BRIDGE_CALL*)(const std::uint8_t*, std::int32_t, std::uint64_t*, std::uint8_t*,
                                                       std::int32_t, std::int32_t*, std::int32_t*);
        using NameFn = std::int32_t(IRIS_BRIDGE_CALL*)(std::uint64_t, std::uint8_t*, std::int32_t, std::int32_t*,
                                                       std::uint8_t*, std::int32_t, std::int32_t*, std::int32_t*);
        using TokenFn = std::int32_t(IRIS_BRIDGE_CALL*)(std::uint64_t, std::uint8_t*, std::int32_t, std::int32_t*,
                                                        std::int32_t*);
        using InvokeFn = std::int32_t(IRIS_BRIDGE_CALL*)(std::uint64_t, const std::uint8_t*, std::int32_t,
                                                         const std::uint8_t*, std::int32_t, std::int32_t, std::int32_t,
                                                         std::int32_t*, std::uint8_t*, std::int32_t, std::int32_t*,
                                                         std::int32_t*);
        using UnloadStateFn = std::int32_t(IRIS_BRIDGE_CALL*)(std::uint64_t, std::int32_t*, std::uint8_t*, std::int32_t,
                                                              std::int32_t*, std::int32_t*);

        using FindTypeFn = std::int32_t(IRIS_BRIDGE_CALL*)(std::uint64_t, const std::uint8_t*, std::int32_t,
                                                           std::uint64_t*, std::uint8_t*, std::int32_t, std::int32_t*,
                                                           std::int32_t*);
        using TypeNameFn = std::int32_t(IRIS_BRIDGE_CALL*)(std::uint64_t, std::uint64_t, std::int32_t, std::uint8_t*,
                                                           std::int32_t, std::int32_t*, std::uint8_t*, std::int32_t,
                                                           std::int32_t*, std::int32_t*);
        using FindMethodFn = std::int32_t(IRIS_BRIDGE_CALL*)(std::uint64_t, std::uint64_t, const std::uint8_t*,
                                                             std::int32_t, std::int32_t, const std::int32_t*,
                                                             std::int32_t, std::uint64_t*, std::uint8_t*, std::int32_t,
                                                             std::int32_t*, std::int32_t*);
        using InvokeMethodFn = std::int32_t(IRIS_BRIDGE_CALL*)(std::uint64_t, std::uint64_t, const std::uint64_t*,
                                                               std::int32_t, std::uint64_t*, std::uint8_t*,
                                                               std::int32_t, std::int32_t*, std::int32_t*);
        using RegisterFn = std::int32_t(IRIS_BRIDGE_CALL*)(const std::uint8_t*, std::int32_t, std::uint8_t*,
                                                           std::int32_t, std::int32_t*, std::int32_t*);

        LoadFn load;
        NameFn copyName;
        TokenFn release;
        InvokeFn invokeInt32;
        UnloadStateFn getUnloadState;
        TokenFn forgetUnload;
        RegisterFn registerShared;
        FindTypeFn findType;
        TypeNameFn copyTypeName;
        FindMethodFn findMethod;
        InvokeMethodFn invokeMethod;
    };
#undef IRIS_BRIDGE_CALL

    inline constexpr std::int32_t BridgeVersion = 12;

    // Managed names of the table slots, in field order, for diagnostics naming a missing entry.
    inline constexpr std::array<const char*, 11> BridgeFunctionNames = {"LoadComponent",
                                                                        "CopyComponentNameUtf8",
                                                                        "ReleaseComponent",
                                                                        "InvokeComponentInt32",
                                                                        "GetComponentUnloadState",
                                                                        "ForgetComponentUnload",
                                                                        "RegisterSharedAssembly",
                                                                        "FindType",
                                                                        "CopyTypeNameUtf8",
                                                                        "FindMethod",
                                                                        "InvokeMethod"};

    static_assert(sizeof(void (*)()) == sizeof(void*), "Bridge slots are copied as object pointers");
    static_assert(sizeof(BridgeFunctions) == BridgeFunctionNames.size() * sizeof(void*),
                  "Every bridge slot needs a name, and the table must have no padding");
} // namespace Iris::Detail
