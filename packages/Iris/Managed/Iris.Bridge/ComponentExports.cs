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

    // Shares an assembly with every component (see SharedAssemblies). The path is explicit-length UTF-8.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int RegisterSharedAssembly(byte* path, int length,
        byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (!ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated) || path == null || length <= 0)
            return (int)BridgeStatus.InvalidArgument;
        try
        {
            SharedAssemblies.Register(new UTF8Encoding(false, true).GetString(new ReadOnlySpan<byte>(path, length)));
            return (int)BridgeStatus.Success;
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)(exception is BridgeArgumentException or DecoderFallbackException
                ? BridgeStatus.InvalidArgument : BridgeStatus.ManagedException);
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

    // Looks up a public type by full name. Success publishes a nonzero handle; NotFound (6) means no
    // such public, closed type is defined in the component assembly. The name is explicit-length UTF-8.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int FindType(ulong token, byte* name, int length, ulong* typeId,
        byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (typeId != null) *typeId = 0;
        if (!ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated) || typeId == null || name == null || length <= 0)
            return (int)BridgeStatus.InvalidArgument;
        string typeName;
        try
        {
            typeName = new UTF8Encoding(false, true).GetString(new ReadOnlySpan<byte>(name, length));
            if (typeName.Contains('\0')) throw new BridgeArgumentException("A type name cannot contain null characters.");
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)(exception is DecoderFallbackException or BridgeArgumentException
                ? BridgeStatus.InvalidArgument : BridgeStatus.ManagedException);
        }
        try
        {
            switch (ComponentRegistry.TryFindType(token, typeName, out ulong found))
            {
                case ComponentRegistry.FindResult.Found:
                    *typeId = found;
                    return (int)BridgeStatus.Success;
                case ComponentRegistry.FindResult.InvalidHandle:
                    return (int)BridgeStatus.InvalidHandle;
                default:
                    ManagedDiagnostic.Write(new TypeLoadException($"No public, closed type named '{typeName}' is defined in the component assembly."),
                        new Span<byte>(diagnostic, capacity), out *written, out *truncated);
                    return (int)BridgeStatus.NotFound;
            }
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)BridgeStatus.ManagedException;
        }
    }

    // Copies a type's full name (field 0) or assembly name (field 1) with the size-query/copy contract of
    // CopyComponentNameUtf8. An unknown handle, or one from another component, is InvalidHandle.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int CopyTypeNameUtf8(ulong token, ulong typeId, int field, byte* destination, int capacity, int* required,
        byte* diagnostic, int diagnosticCapacity, int* written, int* truncated)
    {
        bool diagnosticValid = ManagedDiagnostic.ValidateOutput(diagnostic, diagnosticCapacity, written, truncated);
        bool outputValid = ValidateUtf8Output(destination, capacity, required);
        if (!diagnosticValid || !outputValid || (field != 0 && field != 1)) return (int)BridgeStatus.InvalidArgument;
        try
        {
            if (!ComponentRegistry.TryGetTypeName(token, typeId, field, out string? name)) return (int)BridgeStatus.InvalidHandle;
            return (int)CopyUtf8Payload(name!, Encoding.UTF8, destination, capacity, required);
        }
        catch (Exception exception)
        {
            *required = 0;
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, diagnosticCapacity), out *written, out *truncated);
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
