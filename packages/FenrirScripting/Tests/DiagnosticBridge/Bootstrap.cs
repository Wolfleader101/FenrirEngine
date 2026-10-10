using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Fenrir.ManagedHost;

// Deliberately incompatible responses for checking the real native wrapper.
public static class Bootstrap
{
    private sealed class TextException(string text) : Exception
    {
        public override string ToString() => text;
    }
    private sealed class BrokenException : Exception
    {
        public override string ToString() => throw new InvalidOperationException();
    }
    private static int invocationCount;
    private static int stringMode;
    private static int stringInvocations;
    private static bool componentReleased;
    private static int componentReleaseCount;
    private static int componentForgetCount;

    // FENRIR_TEST_BRIDGE_MODE selects a deliberately incompatible handshake:
    // "legacy" reports an older version, "oversized" a larger table, "missing" a null entry.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int GetBridgeFunctions(int version, void* table, int size, int* reportedVersion, int* reportedSize)
    {
        string? mode;
        try { mode = Environment.GetEnvironmentVariable("FENRIR_TEST_BRIDGE_MODE"); }
        catch (Exception) { return 3; } // Nothing may escape an unmanaged entry point.
        var functions = new BridgeFunctions
        {
            LoadComponent = &LoadComponent,
            CopyComponentNameUtf8 = &CopyComponentNameUtf8,
            ReleaseComponent = &ReleaseComponent,
            InvokeComponentInt32 = &InvokeComponentInt32,
            GetComponentUnloadState = mode == "missing" ? null : &GetComponentUnloadState,
            ForgetComponentUnload = &ForgetComponentUnload
        };
        int answerVersion = mode == "legacy" ? BridgeFunctions.Version - 1 : BridgeFunctions.Version;
        int answerSize = mode == "oversized" ? sizeof(BridgeFunctions) + IntPtr.Size : sizeof(BridgeFunctions);
        return BridgeFunctions.Handshake(version, (BridgeFunctions*)table, size, reportedVersion, reportedSize,
            answerVersion, answerSize, functions);
    }

    private static unsafe int RespondInteger(int left, int right, int* value, byte* diagnostic,
                                            int capacity, int* written, int* truncated)
    {
        *value = 99; *written = 0; *truncated = 0;
        try
        {
            switch (left)
            {
                case 1:
                    ManagedDiagnostic.Write(new TextException(new string('\u03C0', 3000) + "\U0001F43A"),
                                            new Span<byte>(diagnostic, capacity), out *written, out *truncated);
                    return 3;
                case 2:
                    ManagedDiagnostic.Write(new BrokenException(), new Span<byte>(diagnostic, capacity), out *written, out *truncated);
                    return 2;
                case 3: *written = -1; return 3;
                case 4: *written = capacity + 1; return 3;
                case 5: *truncated = 2; return 3;
                case 6: diagnostic[0] = 0xFF; *written = 1; return 3;
                case 7: *written = 1; return 0;
                case 8: return 987;
                case 9: *truncated = 1; return 3;
                case 10:
                    ManagedDiagnostic.Write(new TextException("A\0B"), new Span<byte>(diagnostic, capacity), out *written, out *truncated);
                    return 3;
                case 11: return 5;
                case 12:
                    ++invocationCount;
                    return 3;
                case 100: *value = invocationCount; return 0;
                case 101: *value = stringInvocations; return 0;
                case 102: *value = componentReleaseCount; return 0;
                case 103: *value = componentForgetCount; return 0;
                case >= 200 and <= 240:
                    stringMode = left - 200; stringInvocations = 0; *value = 0; return 0;
                default: *value = checked(left + right); return 0;
            }
        }
        catch (Exception)
        {
            *written = 0; *truncated = 0; return 3;
        }
    }

    // Controlled diagnostic fixture, not a collectible loader. Every load returns the same synthetic
    // registration. InvokeComponentInt32 serves scripted integer responses selected by `left`;
    // CopyComponentNameUtf8 serves scripted UTF-8 query/copy responses selected by the string mode.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int LoadComponent(byte* path, int length, ulong* token, int* collectible,
        byte* diagnostic, int capacity, int* written, int* truncated)
    {
        componentReleased = false; *token = 1; *collectible = 1; *written = 0; *truncated = 0; return 0;
    }
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int CopyComponentNameUtf8(ulong token, byte* output, int capacity, int* required,
        byte* diagnostic, int diagnosticCapacity, int* written, int* truncated)
        => Respond(output, capacity, required, diagnostic, diagnosticCapacity, written, truncated);
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int ReleaseComponent(ulong token, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        ++componentReleaseCount; componentReleased = true; *written = 0; *truncated = 0; return 0;
    }
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int InvokeComponentInt32(ulong token, byte* typeName, int typeLength, byte* methodName, int methodLength,
        int left, int right, int* value, byte* diagnostic, int capacity, int* written, int* truncated)
        => RespondInteger(left, right, value, diagnostic, capacity, written, truncated);
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int GetComponentUnloadState(ulong token, int* state, byte* diagnostic, int capacity, int* written, int* truncated)
        => RespondUnloadState(state, written, truncated);
    private static unsafe int RespondUnloadState(int* state, int* written, int* truncated)
    {
        *state = componentReleased ? 1 : 0; *written = 0; *truncated = 0;
        switch (stringMode)
        {
            case 20: *state = 99; break;
            case 21: *state = -1; break;
            case 22: *state = 2; break;
            case 23: *written = 1; break;
            case 24: return 987;
            case 25: return 5;
            case 26: *state = 0; break;
        }
        return 0;
    }
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int ForgetComponentUnload(ulong token, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        ++componentForgetCount; *written = 0; *truncated = 0; return 0;
    }

    private static unsafe int Respond(byte* output, int capacity, int* required,
                                      byte* diagnostic, int diagnosticCapacity, int* written, int* truncated)
    {
        try
        {
            return RespondCore(output, capacity, required, diagnostic, diagnosticCapacity, written, truncated);
        }
        catch (Exception exception)
        {
            *required = 0;
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, diagnosticCapacity), out *written, out *truncated);
            return 3;
        }
    }

    private static unsafe int RespondCore(byte* output, int capacity, int* required,
                                          byte* diagnostic, int diagnosticCapacity, int* written, int* truncated)
    {
        ++stringInvocations;
        *required = 3; *written = 0; *truncated = 0;
        bool query = output == null;
        if (!query && capacity > 0) output[0] = 0xFF; // A failed copy must never expose this partial payload.
        switch (stringMode)
        {
            case 1 when query:
            case 2 when !query:
                ManagedDiagnostic.Write(new TextException(new string('\u03C0', 3000)),
                    new Span<byte>(diagnostic, diagnosticCapacity), out *written, out *truncated);
                return 3;
            case 3 when query:
            case 4 when !query:
                ManagedDiagnostic.Write(new BrokenException(), new Span<byte>(diagnostic, diagnosticCapacity), out *written, out *truncated);
                return 3;
            case 5 when query: *written = -1; return 3;
            case 6 when !query: diagnostic[0] = 0xFF; *written = 1; return 3;
            case 7 when query:
            case 8 when !query: *written = 1; return 0;
            case 9 when query:
            case 10 when !query: return 987;
            case 11 when !query:
                ManagedDiagnostic.Write(new TextException("Copy buffer too small"), new Span<byte>(diagnostic, diagnosticCapacity), out *written, out *truncated);
                return 4;
            case 12 when query:
                ManagedDiagnostic.Write(new TextException("Invalid component argument"), new Span<byte>(diagnostic, diagnosticCapacity), out *written, out *truncated);
                return 1;
            case 13 when query: *truncated = 1; return 3;
            case 14 when query: *required = -1; return 0;
            case 15 when !query: *required = 2; return 0;
            case 16 when query: *truncated = 1; return 0;
            case 17 when !query: *written = diagnosticCapacity + 1; return 3;
            case 18 when !query: *truncated = 2; return 3;
            case 19 when !query:
                ManagedDiagnostic.Write(new TextException("A\0B"), new Span<byte>(diagnostic, diagnosticCapacity), out *written, out *truncated);
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
