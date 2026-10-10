using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace Fenrir.ManagedHost;

// A scripted stand-in for the bridge, built as Fenrir.ManagedHost, for checking how the native host
// handles every kind of bridge response. Every load returns the same synthetic registration.
//
// The component's InvokeInt32(typeName, methodName, left, right) is the control channel:
//   ("Scenario", <IntegerScenario>) answers with that scripted integer response;
//   ("Select", <Mode>)              selects the response for later name copies and unload queries;
//   ("Counter", <Counter>)          returns a call counter;
//   anything else                   returns left + right.
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

    // FENRIR_TEST_BRIDGE_MODE selects a deliberately incompatible handshake:
    // "legacy" reports an older version, "oversized" a larger table, "missing" a null entry.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int GetBridgeFunctions(int version, void* table, int size, int* reportedVersion, int* reportedSize)
    {
        string? handshake;
        try { handshake = Environment.GetEnvironmentVariable("FENRIR_TEST_BRIDGE_MODE"); }
        catch (Exception) { return 3; } // Nothing may escape an unmanaged entry point.
        var functions = new BridgeFunctions
        {
            LoadComponent = &LoadComponent,
            CopyComponentNameUtf8 = &CopyComponentNameUtf8,
            ReleaseComponent = &ReleaseComponent,
            InvokeComponentInt32 = &InvokeComponentInt32,
            GetComponentUnloadState = handshake == "missing" ? null : &GetComponentUnloadState,
            ForgetComponentUnload = &ForgetComponentUnload
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
    public static unsafe int ForgetComponentUnload(ulong token, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        ++forgets; *written = 0; *truncated = 0; return 0;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int InvokeComponentInt32(ulong token, byte* typeName, int typeLength, byte* methodName, int methodLength,
        int left, int right, int* value, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        *value = 99; *written = 0; *truncated = 0;
        try
        {
            string type = Encoding.UTF8.GetString(typeName, typeLength);
            string method = Encoding.UTF8.GetString(methodName, methodLength);
            if (type == "Scenario")
                return RespondInteger(Enum.Parse<IntegerScenario>(method), value, new Span<byte>(diagnostic, capacity), written, truncated);
            if (type == "Select")
            {
                mode = Enum.Parse<Mode>(method); nameCalls = 0; *value = 0; return 0;
            }
            if (type == "Counter")
            {
                *value = Enum.Parse<Counter>(method) switch
                {
                    Counter.CountedFailures => countedFailures,
                    Counter.NameCalls => nameCalls,
                    Counter.Releases => releases,
                    _ => forgets
                };
                return 0;
            }
            *value = checked(left + right);
            return 0;
        }
        catch (Exception)
        {
            *value = 0; *written = 0; *truncated = 0; return 3;
        }
    }

    private static unsafe int RespondInteger(IntegerScenario scenario, int* value, Span<byte> diagnostic, int* written, int* truncated)
    {
        switch (scenario)
        {
            case IntegerScenario.TruncatedUnicode:
                ManagedDiagnostic.Write(new TextException(new string('π', 3000) + "\U0001F43A"), diagnostic, out *written, out *truncated);
                return 3;
            case IntegerScenario.BrokenFormatter:
                ManagedDiagnostic.Write(new BrokenException(), diagnostic, out *written, out *truncated);
                return 2;
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
