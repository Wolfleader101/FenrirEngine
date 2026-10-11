using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace Iris.Bridge;

// A scripted stand-in for the bridge, built as Iris.Bridge, for checking how the native host
// handles every kind of bridge response. Every load returns the same synthetic registration.
//
// The control channel is a method name. FindMethod remembers the name it was asked for and returns a
// handle to it; InvokeMethod then behaves according to the name:
//   "Scenario:<IntegerScenario>"  answers with that scripted response;
//   "Select:<Mode>"               selects the response for later name copies and unload queries;
//   "Counter:<Counter>"           returns a call counter;
//   anything else                 returns the sum of its two arguments.
public static class Bootstrap
{
    private enum IntegerScenario
    {
        TruncatedUnicode, BrokenFormatter, NegativeCount, OversizedCount, InvalidFlag, InvalidUtf8,
        SuccessWithMetadata, UnknownStatus, EmptyTruncated, EmbeddedNull, InvalidHandle, CountedFailure
    }

    // Responses for CopyComponentNameUtf8 (Query* fail the size query, Copy* fail the copy) and for
    // GetComponentUnloadState (Unload*).
    private enum Mode
    {
        None,
        QueryTruncated, CopyTruncated, QueryBrokenFormatter, CopyBrokenFormatter, QueryNegativeCount,
        CopyInvalidUtf8, QuerySuccessWithMetadata, CopySuccessWithMetadata, QueryUnknownStatus,
        CopyUnknownStatus, CopyBufferTooSmall, QueryInvalidArgument, QueryEmptyTruncated, QueryNegativeLength,
        CopyChangedLength, QuerySuccessTruncated, CopyOversizedCount, CopyInvalidFlag, CopyEmbeddedNull,
        UnloadOutOfRange, UnloadNegative, UnloadCollected, UnloadWithMetadata, UnloadUnknownStatus,
        UnloadInvalidHandle, UnloadNotRequested
    }

    private enum Counter { CountedFailures, NameCalls, Releases, Forgets }

    private sealed class TextException(string text) : Exception
    {
        public override string ToString() => text;
    }
    private sealed class BrokenException : Exception
    {
        public override string ToString() => throw new InvalidOperationException();
    }

    private static Mode mode;
    private static bool released;
    private static int countedFailures, nameCalls, releases, forgets;

    // IRIS_TEST_BRIDGE_MODE selects a deliberately incompatible handshake:
    // "legacy" reports an older version, "oversized" a larger table, "missing" a null entry.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int GetBridgeFunctions(int version, void* table, int size, int* reportedVersion, int* reportedSize)
    {
        string? handshake;
        try { handshake = Environment.GetEnvironmentVariable("IRIS_TEST_BRIDGE_MODE"); }
        catch (Exception) { return 3; } // Nothing may escape an unmanaged entry point.
        var functions = new BridgeFunctions
        {
            LoadComponent = &LoadComponent,
            CopyComponentNameUtf8 = &CopyComponentNameUtf8,
            ReleaseComponent = &ReleaseComponent,
            GetComponentUnloadState = handshake == "missing" ? null : &GetComponentUnloadState,
            ForgetComponentUnload = &ForgetComponentUnload,
            RegisterSharedAssembly = &RegisterSharedAssembly,
            FindType = &FindType,
            CopyTypeNameUtf8 = &CopyTypeNameUtf8,
            FindMethod = &FindMethod,
            InvokeMethod = &InvokeMethod,
            SetLogSink = &SetLogSink,
            ForceReleaseComponent = &ForceReleaseComponent,
            FindInstanceMethod = &FindInstanceMethod,
            InvokeInstanceMethod = &InvokeInstanceMethod,
            CreateObject = &CreateObject,
            ReleaseObject = &ReleaseObject,
            FindMember = &FindMember,
            GetMember = &GetMember,
            SetMember = &SetMember,
            FreeString = &FreeString
        };
        int answerVersion = handshake == "legacy" ? BridgeFunctions.Version - 1 : BridgeFunctions.Version;
        int answerSize = handshake == "oversized" ? sizeof(BridgeFunctions) + IntPtr.Size : sizeof(BridgeFunctions);
        return BridgeFunctions.Handshake(version, (BridgeFunctions*)table, size, reportedVersion, reportedSize,
            answerVersion, answerSize, functions);
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int LoadComponent(byte* path, int length, ulong* token,
        byte* diagnostic, int capacity, int* written, int* truncated)
    {
        released = false; *token = 1; *written = 0; *truncated = 0; return 0;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int ReleaseComponent(ulong token, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        ++releases; released = true; *written = 0; *truncated = 0; return 0;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int RegisterSharedAssembly(byte* path, int length, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        *written = 0; *truncated = 0; return 0;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int FindType(ulong token, byte* name, int length, ulong* typeId, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        *typeId = 1; *written = 0; *truncated = 0; return 0;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int CopyTypeNameUtf8(ulong token, ulong typeId, int field, byte* output, int capacity, int* required,
        byte* diagnostic, int diagnosticCapacity, int* written, int* truncated)
        => CopyName(output, capacity, required, diagnostic, diagnosticCapacity, written, truncated);

    private static readonly List<string> MethodNames = new();

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int FindMethod(ulong token, ulong typeId, byte* name, int nameLength, int returnKind,
        int* paramKinds, int paramCount, ulong* methodId, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        *written = 0; *truncated = 0;
        try
        {
            MethodNames.Add(Encoding.UTF8.GetString(name, nameLength));
            *methodId = (ulong)MethodNames.Count;
            return 0;
        }
        catch (Exception)
        {
            *methodId = 0; return 3;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int InvokeMethod(ulong token, ulong methodId, ulong* arguments, int argCount, ulong* result,
        byte* diagnostic, int capacity, int* written, int* truncated)
    {
        *result = 99; *written = 0; *truncated = 0;
        try
        {
            string name = MethodNames[(int)methodId - 1];
            int colon = name.IndexOf(':');
            string channel = colon < 0 ? name : name.Substring(0, colon);
            string argument = colon < 0 ? "" : name.Substring(colon + 1);
            switch (channel)
            {
                case "Scenario":
                    return RespondInteger(Enum.Parse<IntegerScenario>(argument), result, new Span<byte>(diagnostic, capacity), written, truncated);
                case "Select":
                    mode = Enum.Parse<Mode>(argument); nameCalls = 0; *result = 0; return 0;
                case "Counter":
                    *result = (ulong)(Enum.Parse<Counter>(argument) switch
                    {
                        Counter.CountedFailures => countedFailures,
                        Counter.NameCalls => nameCalls,
                        Counter.Releases => releases,
                        _ => forgets
                    });
                    return 0;
                default:
                    *result = unchecked(arguments[0] + arguments[1]); return 0;
            }
        }
        catch (Exception)
        {
            *result = 0; *written = 0; *truncated = 0; return 3;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int SetLogSink(void* callback, void* context, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        *written = 0; *truncated = 0; return 0;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int ForceReleaseComponent(ulong token, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        ++releases; released = true; *written = 0; *truncated = 0; return 0;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int FindInstanceMethod(ulong token, ulong typeId, byte* name, int nameLength, int returnKind,
        int* paramKinds, int paramCount, ulong* methodId, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        *methodId = 1; *written = 0; *truncated = 0; return 0;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int InvokeInstanceMethod(ulong token, ulong methodId, ulong objectId, ulong* arguments, int argCount, ulong* result,
        byte* diagnostic, int capacity, int* written, int* truncated)
    {
        *result = 0; *written = 0; *truncated = 0; return 0;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int CreateObject(ulong token, ulong typeId, int* paramKinds, ulong* arguments, int count, ulong* objectId,
        byte* diagnostic, int capacity, int* written, int* truncated)
    {
        *objectId = 1; *written = 0; *truncated = 0; return 0;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int ReleaseObject(ulong token, ulong objectId, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        *written = 0; *truncated = 0; return 0;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int FindMember(ulong token, ulong typeId, byte* name, int nameLength, int memberKind, int valueKind,
        ulong* memberId, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        *memberId = 1; *written = 0; *truncated = 0; return 0;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int GetMember(ulong token, ulong memberId, ulong objectId, ulong* value,
        byte* diagnostic, int capacity, int* written, int* truncated)
    {
        *value = 0; *written = 0; *truncated = 0; return 0;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static void FreeString(ulong block)
    {
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int SetMember(ulong token, ulong memberId, ulong objectId, ulong value,
        byte* diagnostic, int capacity, int* written, int* truncated)
    {
        *written = 0; *truncated = 0; return 0;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int ForgetComponentUnload(ulong token, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        ++forgets; *written = 0; *truncated = 0; return 0;
    }

    private static unsafe int RespondInteger(IntegerScenario scenario, ulong* value, Span<byte> diagnostic, int* written, int* truncated)
    {
        switch (scenario)
        {
            case IntegerScenario.TruncatedUnicode:
                ManagedDiagnostic.Write(new TextException(new string('π', 3000) + "\U0001F43A"), diagnostic, out *written, out *truncated);
                return 3;
            case IntegerScenario.BrokenFormatter:
                ManagedDiagnostic.Write(new BrokenException(), diagnostic, out *written, out *truncated);
                return 1;
            case IntegerScenario.NegativeCount: *written = -1; return 3;
            case IntegerScenario.OversizedCount: *written = diagnostic.Length + 1; return 3;
            case IntegerScenario.InvalidFlag: *truncated = 2; return 3;
            case IntegerScenario.InvalidUtf8: diagnostic[0] = 0xFF; *written = 1; return 3;
            case IntegerScenario.SuccessWithMetadata: *value = 1; *written = 1; return 0;
            case IntegerScenario.UnknownStatus: return 987;
            case IntegerScenario.EmptyTruncated: *truncated = 1; return 3;
            case IntegerScenario.EmbeddedNull:
                ManagedDiagnostic.Write(new TextException("A\0B"), diagnostic, out *written, out *truncated);
                return 3;
            case IntegerScenario.InvalidHandle: return 5;
            case IntegerScenario.CountedFailure: ++countedFailures; return 3;
            default: throw new ArgumentOutOfRangeException(nameof(scenario));
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int GetComponentUnloadState(ulong token, int* state, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        *state = released ? 1 : 0; *written = 0; *truncated = 0;
        switch (mode)
        {
            case Mode.UnloadOutOfRange: *state = 99; break;
            case Mode.UnloadNegative: *state = -1; break;
            case Mode.UnloadCollected: *state = 2; break;
            case Mode.UnloadWithMetadata: *written = 1; break;
            case Mode.UnloadUnknownStatus: return 987;
            case Mode.UnloadInvalidHandle: return 5;
            case Mode.UnloadNotRequested: *state = 0; break;
        }
        return 0;
    }

    // Serves the name "A\0B" (3 bytes) unless the mode scripts a failure.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int CopyComponentNameUtf8(ulong token, byte* output, int capacity, int* required,
        byte* diagnostic, int diagnosticCapacity, int* written, int* truncated)
        => CopyName(output, capacity, required, diagnostic, diagnosticCapacity, written, truncated);

    private static unsafe int CopyName(byte* output, int capacity, int* required,
        byte* diagnostic, int diagnosticCapacity, int* written, int* truncated)
    {
        try
        {
            return RespondName(output, capacity, required, new Span<byte>(diagnostic, diagnosticCapacity), written, truncated);
        }
        catch (Exception exception)
        {
            *required = 0;
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, diagnosticCapacity), out *written, out *truncated);
            return 3;
        }
    }

    private static unsafe int RespondName(byte* output, int capacity, int* required, Span<byte> diagnostic,
                                          int* written, int* truncated)
    {
        ++nameCalls;
        *required = 3; *written = 0; *truncated = 0;
        bool query = output == null;
        if (!query && capacity > 0) output[0] = 0xFF; // A failed copy must never expose this partial payload.
        switch (mode)
        {
            case Mode.QueryTruncated when query:
            case Mode.CopyTruncated when !query:
                ManagedDiagnostic.Write(new TextException(new string('π', 3000)), diagnostic, out *written, out *truncated);
                return 3;
            case Mode.QueryBrokenFormatter when query:
            case Mode.CopyBrokenFormatter when !query:
                ManagedDiagnostic.Write(new BrokenException(), diagnostic, out *written, out *truncated);
                return 3;
            case Mode.QueryNegativeCount when query: *written = -1; return 3;
            case Mode.CopyInvalidUtf8 when !query: diagnostic[0] = 0xFF; *written = 1; return 3;
            case Mode.QuerySuccessWithMetadata when query:
            case Mode.CopySuccessWithMetadata when !query: *written = 1; return 0;
            case Mode.QueryUnknownStatus when query:
            case Mode.CopyUnknownStatus when !query: return 987;
            case Mode.CopyBufferTooSmall when !query:
                ManagedDiagnostic.Write(new TextException("Copy buffer too small"), diagnostic, out *written, out *truncated);
                return 4;
            case Mode.QueryInvalidArgument when query:
                ManagedDiagnostic.Write(new TextException("Invalid component argument"), diagnostic, out *written, out *truncated);
                return 1;
            case Mode.QueryEmptyTruncated when query: *truncated = 1; return 3;
            case Mode.QueryNegativeLength when query: *required = -1; return 0;
            case Mode.CopyChangedLength when !query: *required = 2; return 0;
            case Mode.QuerySuccessTruncated when query: *truncated = 1; return 0;
            case Mode.CopyOversizedCount when !query: *written = diagnostic.Length + 1; return 3;
            case Mode.CopyInvalidFlag when !query: *truncated = 2; return 3;
            case Mode.CopyEmbeddedNull when !query:
                ManagedDiagnostic.Write(new TextException("A\0B"), diagnostic, out *written, out *truncated);
                return 3;
        }
        if (!query)
        {
            if (capacity < 3) return 4;
            output[0] = (byte)'A'; output[1] = 0; output[2] = (byte)'B';
        }
        return 0;
    }
}
