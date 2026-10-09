using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace Fenrir.ManagedHost;

public static partial class Bootstrap
{
    private const string Greeting = "Hello from .NET \u2014 \u03C0 \U0001F43A";

    // The native host resolves this method through its assembly-loading runtime delegate.
    // This is not a native DLL export and must not be called directly from C#.
    // The native function pointer must use the matching int32_t(), Cdecl signature.
    // Keep this handshake free of exceptions; unmanaged callers cannot catch managed exceptions.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetBridgeVersion() => 4;

    // All output storage is writable, non-overlapping, and borrowed only
    // during this call. Counts exclude a terminator. Null diagnostic is allowed
    // only for zero capacity; written/truncated are mandatory int32 outputs.
    // Truncation is 0 or 1 and never changes the operation's status.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int TryAddInt32(int left, int right, int* result,
                                       byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (result != null) *result = 0;
        bool diagnosticValid = ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated);
        if (result == null || !diagnosticValid)
        {
            return (int)BridgeStatus.InvalidArgument;
        }

        try
        {
            *result = checked(left + right);
            return (int)BridgeStatus.Success;
        }
        catch (OverflowException exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)BridgeStatus.Overflow;
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)BridgeStatus.ManagedException;
        }
    }

    // Sizes count UTF-8 bytes, excluding any BOM or terminator. A null destination
    // with zero capacity queries the size. Insufficient capacity writes no payload.
    // requiredBytes and destination must refer to valid, non-overlapping native storage.
    // No pointer is retained. All storage, including diagnostic metadata, must not
    // overlap. The immutable greeting is stable between query and copy.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int CopyGreetingUtf8(byte* destination, int capacity, int* requiredBytes,
                                             byte* diagnostic, int diagnosticCapacity, int* written, int* truncated)
    {
        bool diagnosticValid = ManagedDiagnostic.ValidateOutput(diagnostic, diagnosticCapacity, written, truncated);
        bool outputValid = ValidateUtf8Output(destination, capacity, requiredBytes);
        if (!diagnosticValid || !outputValid)
        {
            return (int)BridgeStatus.InvalidArgument;
        }

        try
        {
            return (int)CopyUtf8Payload(Greeting, Encoding.UTF8, destination, capacity, requiredBytes);
        }
        catch (Exception exception)
        {
            *requiredBytes = 0;
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, diagnosticCapacity), out *written, out *truncated);
            return (int)BridgeStatus.ManagedException;
        }
    }

    // Native input is read-only and borrowed during this call. Lengths count bytes.
    // Null input is valid only for length zero. Input, destination, and count storage
    // must not overlap. The caller must keep input unchanged between query and copy.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int EchoUtf8(byte* input, int inputLength, byte* destination, int capacity, int* requiredBytes,
                                     byte* diagnostic, int diagnosticCapacity, int* written, int* truncated)
    {
        bool diagnosticValid = ManagedDiagnostic.ValidateOutput(diagnostic, diagnosticCapacity, written, truncated);
        bool outputValid = ValidateUtf8Output(destination, capacity, requiredBytes);
        if (!diagnosticValid || !outputValid)
        {
            return (int)BridgeStatus.InvalidArgument;
        }
        if (inputLength < 0 || (input == null && inputLength != 0))
        {
            return (int)BridgeStatus.InvalidArgument;
        }

        try
        {
            // Construct inside the try: encoding initialization failures must also
            // remain inside the native-callable export's exception handling.
            Encoding encoding = new UTF8Encoding(false, true);
            string text = encoding.GetString(new ReadOnlySpan<byte>(input, inputLength));
            return (int)CopyUtf8Payload(text, encoding, destination, capacity, requiredBytes);
        }
        catch (DecoderFallbackException exception)
        {
            *requiredBytes = 0;
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, diagnosticCapacity), out *written, out *truncated);
            return (int)BridgeStatus.InvalidArgument;
        }
        catch (Exception exception)
        {
            *requiredBytes = 0;
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, diagnosticCapacity), out *written, out *truncated);
            return (int)BridgeStatus.ManagedException;
        }
    }

    private static unsafe bool ValidateUtf8Output(byte* destination, int capacity, int* requiredBytes)
    {
        if (requiredBytes == null)
        {
            return false;
        }
        *requiredBytes = 0;
        return capacity >= 0 && (destination != null || capacity == 0);
    }

    // Called only after pointer/capacity validation, inside an export's try block.
    private static unsafe BridgeStatus CopyUtf8Payload(string text, Encoding encoding, byte* destination,
                                                      int capacity, int* requiredBytes)
    {
        int byteCount = encoding.GetByteCount(text);
        *requiredBytes = byteCount;
        if (destination == null)
        {
            return BridgeStatus.Success;
        }
        if (capacity < byteCount)
        {
            return BridgeStatus.BufferTooSmall;
        }
        // The span borrows caller-owned memory and covers only the payload.
        encoding.GetBytes(text.AsSpan(), new Span<byte>(destination, byteCount));
        return BridgeStatus.Success;
    }
}
