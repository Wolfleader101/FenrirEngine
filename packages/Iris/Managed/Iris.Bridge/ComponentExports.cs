using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace Iris.Bridge;

public static partial class Bootstrap
{
    // The token and diagnostic storage are writable and non-overlapping. No pointer is retained.
    // A token is published only after registration succeeds.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int LoadComponent(byte* path, int length, ulong* token,
        byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (token != null) *token = 0;
        bool diagnosticValid = ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated);
        if (!diagnosticValid || token == null || path == null || length <= 0)
            return (int)BridgeStatus.InvalidArgument;
        string assemblyPath;
        try
        {
            assemblyPath = new UTF8Encoding(false, true).GetString(new ReadOnlySpan<byte>(path, length));
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)(exception is DecoderFallbackException ? BridgeStatus.InvalidArgument : BridgeStatus.ManagedException);
        }
        try
        {
            *token = ComponentRegistry.Load(assemblyPath);
            return (int)BridgeStatus.Success;
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)(exception is BridgeArgumentException ? BridgeStatus.InvalidArgument : BridgeStatus.ManagedException);
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int CopyComponentNameUtf8(ulong token, byte* destination, int capacity, int* required,
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
    internal static unsafe int ReleaseComponent(ulong token, byte* diagnostic, int capacity, int* written, int* truncated)
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

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int GetComponentUnloadState(ulong token, int* state, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (state != null) *state = -1;
        bool diagnosticValid = ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated);
        if (!diagnosticValid || state == null) return (int)BridgeStatus.InvalidArgument;
        try
        {
            if (!ComponentRegistry.TryGetUnloadState(token, out ComponentUnloadState observed)) return (int)BridgeStatus.InvalidHandle;
            *state = (int)observed;
            return (int)BridgeStatus.Success;
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)BridgeStatus.ManagedException;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int ForgetComponentUnload(ulong token, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (!ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated)) return (int)BridgeStatus.InvalidArgument;
        try
        {
            return (int)(ComponentRegistry.ForgetUnload(token) ? BridgeStatus.Success : BridgeStatus.InvalidHandle);
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)BridgeStatus.ManagedException;
        }
    }

    // Names are explicit-length UTF-8, borrowed only for this call. All caller
    // storage must be valid and non-overlapping. Only success publishes a value.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int InvokeComponentInt32(ulong token, byte* typeName, int typeLength, byte* methodName, int methodLength,
        int left, int right, int* value, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (value != null) *value = 0;
        bool diagnosticValid = ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated);
        if (!diagnosticValid || value == null || typeName == null || typeLength <= 0 || methodName == null || methodLength <= 0)
            return (int)BridgeStatus.InvalidArgument;

        string type, method;
        try
        {
            var utf8 = new UTF8Encoding(false, true);
            type = utf8.GetString(new ReadOnlySpan<byte>(typeName, typeLength));
            method = utf8.GetString(new ReadOnlySpan<byte>(methodName, methodLength));
            if (type.Contains('\0') || method.Contains('\0')) return (int)BridgeStatus.InvalidArgument;
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)(exception is DecoderFallbackException ? BridgeStatus.InvalidArgument : BridgeStatus.ManagedException);
        }
        try
        {
            if (!ComponentRegistry.TryInvokeInt32(token, type, method, left, right, out int result))
                return (int)BridgeStatus.InvalidHandle;
            *value = result;
            return (int)BridgeStatus.Success;
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)(exception is OverflowException ? BridgeStatus.Overflow : BridgeStatus.ManagedException);
        }
    }
}
