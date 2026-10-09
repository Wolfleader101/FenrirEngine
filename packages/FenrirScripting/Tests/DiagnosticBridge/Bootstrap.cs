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

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetBridgeVersion()
    {
#if LEGACY_BRIDGE
        return 3;
#else
        return 4;
#endif
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int TryAddInt32(int left, int right, int* value, byte* diagnostic,
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
                case 12:
                    ++invocationCount;
                    return 3;
                case 100: *value = invocationCount; return 0;
                case 101: *value = stringInvocations; return 0;
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

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int CopyGreetingUtf8(byte* output, int capacity, int* required,
                                             byte* diagnostic, int diagnosticCapacity, int* written, int* truncated)
        => Respond(output, capacity, required, diagnostic, diagnosticCapacity, written, truncated);
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int EchoUtf8(byte* input, int length, byte* output, int capacity, int* required,
                                     byte* diagnostic, int diagnosticCapacity, int* written, int* truncated)
        => Respond(output, capacity, required, diagnostic, diagnosticCapacity, written, truncated);

#if !MISSING_COMPONENT_METHODS
    // Diagnostic-only fixture: these entry points are resolved but never invoked.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int LoadComponent(byte* path, int length, ulong* token, int* collectible,
        byte* diagnostic, int capacity, int* written, int* truncated) => 1;
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int CopyComponentNameUtf8(ulong token, byte* output, int capacity, int* required,
        byte* diagnostic, int diagnosticCapacity, int* written, int* truncated) => 1;
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int ReleaseComponent(ulong token, byte* diagnostic, int capacity, int* written, int* truncated) => 1;
#endif

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
