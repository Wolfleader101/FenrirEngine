using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace Fenrir.ManagedHost;

public static partial class Bootstrap
{
    // ABI v4. Token/flags and diagnostic storage are writable and non-overlapping.
    // No pointer is retained. A token is published only after registration succeeds.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int LoadComponent(byte* path, int length, ulong* token, int* collectible,
        byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (token != null) *token = 0;
        if (collectible != null) *collectible = 0;
        bool diagnosticValid = ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated);
        if (!diagnosticValid || token == null || collectible == null || path == null || length <= 0)
            return (int)BridgeStatus.InvalidArgument;
        try
        {
            string assemblyPath = new UTF8Encoding(false, true).GetString(new ReadOnlySpan<byte>(path, length));
            ulong ownedToken = ComponentRegistry.Load(assemblyPath);
            *token = ownedToken;
            *collectible = 1; // Registry verified the actual assembly and context before publication.
            return (int)BridgeStatus.Success;
        }
        catch (ArgumentException exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)BridgeStatus.InvalidArgument;
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)BridgeStatus.ManagedException;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int CopyComponentNameUtf8(ulong token, byte* destination, int capacity, int* required,
        byte* diagnostic, int diagnosticCapacity, int* written, int* truncated)
    {
        bool diagnosticValid = ManagedDiagnostic.ValidateOutput(diagnostic, diagnosticCapacity, written, truncated);
        bool outputValid = ValidateUtf8Output(destination, capacity, required);
        if (!diagnosticValid || !outputValid) return (int)BridgeStatus.InvalidArgument;
        try
        {
            string? name = ComponentRegistry.GetName(token);
            if (name == null) return (int)BridgeStatus.InvalidHandle;
            return (int)CopyUtf8Payload(name, Encoding.UTF8, destination, capacity, required);
        }
        catch (Exception exception)
        {
            *required = 0;
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, diagnosticCapacity), out *written, out *truncated);
            return (int)BridgeStatus.ManagedException;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int ReleaseComponent(ulong token, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (!ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated)) return (int)BridgeStatus.InvalidArgument;
        try
        {
            return (int)(ComponentRegistry.Release(token) ? BridgeStatus.Success : BridgeStatus.InvalidHandle);
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)BridgeStatus.ManagedException;
        }
    }
}
