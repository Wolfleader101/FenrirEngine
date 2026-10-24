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
        // Called by the bridge: (context, LogLevel, UTF-8 text, byte count). Must not throw.
        using LogCallbackFn = void(IRIS_BRIDGE_CALL*)(void*, std::int32_t, const std::uint8_t*, std::int32_t);
        using SetLogSinkFn = std::int32_t(IRIS_BRIDGE_CALL*)(LogCallbackFn, void*, std::uint8_t*, std::int32_t,
                                                             std::int32_t*, std::int32_t*);
        using InvokeInstanceMethodFn = std::int32_t(IRIS_BRIDGE_CALL*)(std::uint64_t, std::uint64_t, std::uint64_t,
                                                                       const std::uint64_t*, std::int32_t,
                                                                       std::uint64_t*, std::uint8_t*, std::int32_t,
                                                                       std::int32_t*, std::int32_t*);
        using CreateObjectFn = std::int32_t(IRIS_BRIDGE_CALL*)(std::uint64_t, std::uint64_t, const std::int32_t*,
                                                               const std::uint64_t*, std::int32_t, std::uint64_t*,
                                                               std::uint8_t*, std::int32_t, std::int32_t*,
                                                               std::int32_t*);
        using ReleaseObjectFn = std::int32_t(IRIS_BRIDGE_CALL*)(std::uint64_t, std::uint64_t, std::uint8_t*,
                                                                std::int32_t, std::int32_t*, std::int32_t*);
        using FindMemberFn = std::int32_t(IRIS_BRIDGE_CALL*)(std::uint64_t, std::uint64_t, const std::uint8_t*,
                                                             std::int32_t, std::int32_t, std::int32_t, std::uint64_t*,
                                                             std::uint8_t*, std::int32_t, std::int32_t*, std::int32_t*);
        using GetMemberFn = std::int32_t(IRIS_BRIDGE_CALL*)(std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t*,
                                                            std::uint8_t*, std::int32_t, std::int32_t*, std::int32_t*);
        using SetMemberFn = std::int32_t(IRIS_BRIDGE_CALL*)(std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t,
                                                            std::uint8_t*, std::int32_t, std::int32_t*, std::int32_t*);
        using FreeStringFn = void(IRIS_BRIDGE_CALL*)(std::uint64_t);
        using RegisterStructFn = std::int32_t(IRIS_BRIDGE_CALL*)(std::uint64_t, const std::uint8_t*, std::int32_t,
                                                                 std::uint64_t*, std::uint8_t*, std::int32_t,
                                                                 std::int32_t*, std::int32_t*);
        using CreateArrayFn = std::int32_t(IRIS_BRIDGE_CALL*)(std::uint64_t, std::int32_t, std::int64_t, std::uint64_t*,
                                                              std::uint8_t*, std::int32_t, std::int32_t*,
                                                              std::int32_t*);
        using ArrayLengthFn = std::int32_t(IRIS_BRIDGE_CALL*)(std::uint64_t, std::uint64_t, std::int64_t*,
                                                              std::uint8_t*, std::int32_t, std::int32_t*,
                                                              std::int32_t*);
        using ArrayGetFn = std::int32_t(IRIS_BRIDGE_CALL*)(std::uint64_t, std::uint64_t, std::int32_t, std::int64_t,
                                                           std::uint64_t*, std::uint8_t*, std::int32_t, std::int32_t*,
                                                           std::int32_t*);
        using ArraySetFn = std::int32_t(IRIS_BRIDGE_CALL*)(std::uint64_t, std::uint64_t, std::int32_t, std::int64_t,
                                                           std::uint64_t, std::uint8_t*, std::int32_t, std::int32_t*,
                                                           std::int32_t*);
        using ArrayCopyFn = std::int32_t(IRIS_BRIDGE_CALL*)(std::uint64_t, std::uint64_t, std::int32_t, std::int64_t,
                                                            std::int64_t, void*, std::int32_t, std::uint8_t*,
                                                            std::int32_t, std::int32_t*, std::int32_t*);
        using RegisterInternalCallFn = std::int32_t(IRIS_BRIDGE_CALL*)(const std::uint8_t*, std::int32_t, std::int32_t,
                                                                       const std::int32_t*, std::int32_t, void*,
                                                                       std::uint8_t*, std::int32_t, std::int32_t*,
                                                                       std::int32_t*);
        using ListTypesFn = std::int32_t(IRIS_BRIDGE_CALL*)(std::uint64_t, std::uint64_t*, std::uint8_t*, std::int32_t,
                                                            std::int32_t*, std::int32_t*);
        using QueryTypeFn = std::int32_t(IRIS_BRIDGE_CALL*)(std::uint64_t, std::uint64_t, std::int32_t, std::uint64_t*,
                                                            std::uint8_t*, std::int32_t, std::int32_t*, std::int32_t*);
        using IsTypeAssignableFn = std::int32_t(IRIS_BRIDGE_CALL*)(std::uint64_t, std::uint64_t, std::uint64_t,
                                                                   const std::uint8_t*, std::int32_t, std::int32_t*,
                                                                   std::uint8_t*, std::int32_t, std::int32_t*,
                                                                   std::int32_t*);
        using WaitForUnloadFn = std::int32_t(IRIS_BRIDGE_CALL*)(std::uint64_t, std::int32_t, std::int32_t*,
                                                                std::uint8_t*, std::int32_t, std::int32_t*,
                                                                std::int32_t*);
        using RegisterFn = std::int32_t(IRIS_BRIDGE_CALL*)(const std::uint8_t*, std::int32_t, std::uint8_t*,
                                                           std::int32_t, std::int32_t*, std::int32_t*);

        LoadFn load;
        NameFn copyName;
        TokenFn release;
        UnloadStateFn getUnloadState;
        TokenFn forgetUnload;
        RegisterFn registerShared;
        FindTypeFn findType;
        TypeNameFn copyTypeName;
        FindMethodFn findMethod;
        InvokeMethodFn invokeMethod;
        SetLogSinkFn setLogSink;
        TokenFn forceRelease;
        FindMethodFn findInstanceMethod;
        InvokeInstanceMethodFn invokeInstanceMethod;
        CreateObjectFn createObject;
        ReleaseObjectFn releaseObject;
        FindMemberFn findMember;
        GetMemberFn getMember;
        SetMemberFn setMember;
        FreeStringFn freeString;
        RegisterStructFn registerStruct;
        CreateArrayFn createArray;
        ArrayLengthFn arrayLength;
        ArrayGetFn arrayGet;
        ArraySetFn arraySet;
        ArrayCopyFn arrayCopy;
        RegisterInternalCallFn registerInternalCall;
        ListTypesFn listTypes;
        QueryTypeFn queryType;
        IsTypeAssignableFn isTypeAssignable;
        WaitForUnloadFn waitForUnload;
    };
#undef IRIS_BRIDGE_CALL

    inline constexpr std::int32_t BridgeVersion = 22;

    // The bridge's wire status 4 (BufferTooSmall). Native code always sizes buffers from the bridge's own
    // answer, so the bridge reporting it is a malformed response; it is not a public CallStatus.
    inline constexpr std::int32_t WireBufferTooSmall = 4;

    // Managed names of the table slots, in field order, for diagnostics naming a missing entry.
    inline constexpr std::array<const char*, 31> BridgeFunctionNames = {"LoadComponent",
                                                                        "CopyComponentNameUtf8",
                                                                        "ReleaseComponent",
                                                                        "GetComponentUnloadState",
                                                                        "ForgetComponentUnload",
                                                                        "RegisterSharedAssembly",
                                                                        "FindType",
                                                                        "CopyTypeNameUtf8",
                                                                        "FindMethod",
                                                                        "InvokeMethod",
                                                                        "SetLogSink",
                                                                        "ForceReleaseComponent",
                                                                        "FindInstanceMethod",
                                                                        "InvokeInstanceMethod",
                                                                        "CreateObject",
                                                                        "ReleaseObject",
                                                                        "FindMember",
                                                                        "GetMember",
                                                                        "SetMember",
                                                                        "FreeString",
                                                                        "RegisterStruct",
                                                                        "CreateArray",
                                                                        "GetArrayLength",
                                                                        "GetArrayElement",
                                                                        "SetArrayElement",
                                                                        "CopyArray",
                                                                        "RegisterInternalCall",
                                                                        "ListTypes",
                                                                        "QueryType",
                                                                        "IsTypeAssignable",
                                                                        "WaitForComponentUnload"};

    static_assert(sizeof(void (*)()) == sizeof(void*), "Bridge slots are copied as object pointers");
    static_assert(sizeof(BridgeFunctions) == BridgeFunctionNames.size() * sizeof(void*),
                  "Every bridge slot needs a name, and the table must have no padding");
} // namespace Iris::Detail
