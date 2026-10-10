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

    // Registers (or, with a null callback, clears) the process-wide log sink. The callback receives the
    // context, a LogLevel, and explicit-length UTF-8 text borrowed for the call. It runs synchronously on
    // the thread making the bridge call and must not throw. The context must stay valid until cleared.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int SetLogSink(void* callback, void* context, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (!ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated)) return (int)BridgeStatus.InvalidArgument;
        BridgeLog.Set((IntPtr)callback, (IntPtr)context);
        return (int)BridgeStatus.Success;
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

    // Releases a component and requests cooperative unloading. InUse (7) means it still has live objects
    // and stays loaded; ForceReleaseComponent releases it anyway and invalidates those objects.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int ReleaseComponent(ulong token, byte* diagnostic, int capacity, int* written, int* truncated)
        => ReleaseCore(token, force: false, diagnostic, capacity, written, truncated);

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int ForceReleaseComponent(ulong token, byte* diagnostic, int capacity, int* written, int* truncated)
        => ReleaseCore(token, force: true, diagnostic, capacity, written, truncated);

    private static unsafe int ReleaseCore(ulong token, bool force, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (!ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated)) return (int)BridgeStatus.InvalidArgument;
        try
        {
            return (int)(ComponentRegistry.TryRelease(token, force) switch
            {
                ComponentRegistry.ReleaseResult.Released => BridgeStatus.Success,
                ComponentRegistry.ReleaseResult.InUse => BridgeStatus.InUse,
                _ => BridgeStatus.InvalidHandle
            });
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
                        new Span<byte>(diagnostic, capacity), out *written, out *truncated, report: false);
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

    // Finds a public static method on a looked-up type by name, return kind and exact parameter kinds
    // (see ValueKind). Success publishes a nonzero handle; NotFound (6) means no such method exists.
    // The name is explicit-length UTF-8; paramKinds holds paramCount int32 kinds and may be null for none.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int FindMethod(ulong token, ulong typeId, byte* name, int nameLength, int returnKind,
        int* paramKinds, int paramCount, ulong* methodId, byte* diagnostic, int capacity, int* written, int* truncated)
        => FindMethodCore(token, typeId, name, nameLength, returnKind, paramKinds, paramCount, isStatic: true, methodId, diagnostic, capacity, written, truncated);

    // Like FindMethod, for a public instance method, which may be inherited. Call it with
    // InvokeInstanceMethod on an object that is an instance of the declaring type.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int FindInstanceMethod(ulong token, ulong typeId, byte* name, int nameLength, int returnKind,
        int* paramKinds, int paramCount, ulong* methodId, byte* diagnostic, int capacity, int* written, int* truncated)
        => FindMethodCore(token, typeId, name, nameLength, returnKind, paramKinds, paramCount, isStatic: false, methodId, diagnostic, capacity, written, truncated);

    private static unsafe int FindMethodCore(ulong token, ulong typeId, byte* name, int nameLength, int returnKind,
        int* paramKinds, int paramCount, bool isStatic, ulong* methodId, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (methodId != null) *methodId = 0;
        if (!ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated) || methodId == null ||
            name == null || nameLength <= 0 || paramCount < 0 || (paramCount > 0 && paramKinds == null))
            return (int)BridgeStatus.InvalidArgument;
        string methodName;
        int[] kinds;
        try
        {
            // The CLR limits a method to 255 parameters; this also bounds the allocation below.
            if (paramCount > 255) throw new BridgeArgumentException("A method cannot have more than 255 parameters.");
            kinds = new int[paramCount];
            methodName = new UTF8Encoding(false, true).GetString(new ReadOnlySpan<byte>(name, nameLength));
            if (methodName.Contains('\0')) throw new BridgeArgumentException("A method name cannot contain null characters.");
            if (!ValueKinds.IsValid(returnKind)) throw new BridgeArgumentException($"Unsupported return kind {returnKind}.");
            for (int index = 0; index < paramCount; ++index)
            {
                kinds[index] = paramKinds[index];
                ValueKinds.ToType(kinds[index], allowVoid: false);
            }
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)(exception is DecoderFallbackException or BridgeArgumentException
                ? BridgeStatus.InvalidArgument : BridgeStatus.ManagedException);
        }
        try
        {
            switch (ComponentRegistry.TryFindMethod(token, typeId, methodName, returnKind, kinds, isStatic, out ulong found))
            {
                case ComponentRegistry.FindResult.Found:
                    *methodId = found;
                    return (int)BridgeStatus.Success;
                case ComponentRegistry.FindResult.InvalidHandle:
                    return (int)BridgeStatus.InvalidHandle;
                default:
                    ManagedDiagnostic.Write(new MissingMethodException($"No public static, non-generic method '{methodName}' with the requested return and parameter types was found."),
                        new Span<byte>(diagnostic, capacity), out *written, out *truncated, report: false);
                    return (int)BridgeStatus.NotFound;
            }
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)BridgeStatus.ManagedException;
        }
    }

    // Runs a found method. arguments holds argCount 64-bit slots, one per parameter; the result slot is
    // zero for void. Component exceptions (including OverflowException and ArgumentException) are
    // ManagedException; only the bridge's own validation is InvalidArgument.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int InvokeMethod(ulong token, ulong methodId, ulong* arguments, int argCount, ulong* result,
        byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (result != null) *result = 0;
        if (!ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated) || result == null ||
            argCount < 0 || (argCount > 0 && arguments == null))
            return (int)BridgeStatus.InvalidArgument;
        try
        {
            if (!ComponentRegistry.TryInvokeMethod(token, methodId, new ReadOnlySpan<ulong>(arguments, argCount), out ulong returned))
                return (int)BridgeStatus.InvalidHandle;
            *result = returned;
            return (int)BridgeStatus.Success;
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)(exception is BridgeArgumentException ? BridgeStatus.InvalidArgument : BridgeStatus.ManagedException);
        }
    }

    // Runs an instance method on an owned object. Like InvokeMethod, except the object handle must belong
    // to this component and be an instance of the method's declaring type (else InvalidArgument).
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int InvokeInstanceMethod(ulong token, ulong methodId, ulong objectId, ulong* arguments, int argCount, ulong* result,
        byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (result != null) *result = 0;
        if (!ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated) || result == null ||
            argCount < 0 || (argCount > 0 && arguments == null))
            return (int)BridgeStatus.InvalidArgument;
        try
        {
            if (!ComponentRegistry.TryInvokeInstanceMethod(token, methodId, objectId, new ReadOnlySpan<ulong>(arguments, argCount), out ulong returned))
                return (int)BridgeStatus.InvalidHandle;
            *result = returned;
            return (int)BridgeStatus.Success;
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)(exception is BridgeArgumentException ? BridgeStatus.InvalidArgument : BridgeStatus.ManagedException);
        }
    }

    // Constructs an object of a looked-up type with the public constructor whose parameter kinds match
    // exactly; paramKinds and arguments both hold count entries. Success publishes a nonzero object handle
    // that stays rooted until ReleaseObject (or until the component is released). NotFound (6): no such
    // constructor, or the type is abstract. A constructor exception is ManagedException and roots nothing.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int CreateObject(ulong token, ulong typeId, int* paramKinds, ulong* arguments, int count, ulong* objectId,
        byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (objectId != null) *objectId = 0;
        if (!ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated) || objectId == null ||
            count < 0 || (count > 0 && (paramKinds == null || arguments == null)))
            return (int)BridgeStatus.InvalidArgument;
        try
        {
            if (count > 255) throw new BridgeArgumentException("A constructor cannot have more than 255 parameters.");
            int[] kinds = new int[count];
            for (int index = 0; index < count; ++index)
            {
                kinds[index] = paramKinds[index];
                ValueKinds.ToType(kinds[index], allowVoid: false);
            }
            switch (ComponentRegistry.TryCreateObject(token, typeId, kinds, new ReadOnlySpan<ulong>(arguments, count), out ulong created))
            {
                case ComponentRegistry.CreateResult.Created:
                    *objectId = created;
                    return (int)BridgeStatus.Success;
                case ComponentRegistry.CreateResult.InvalidHandle:
                    return (int)BridgeStatus.InvalidHandle;
                default:
                    ManagedDiagnostic.Write(new MissingMethodException("No public constructor with the requested parameter types was found on a concrete type."),
                        new Span<byte>(diagnostic, capacity), out *written, out *truncated, report: false);
                    return (int)BridgeStatus.NotFound;
            }
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)(exception is BridgeArgumentException ? BridgeStatus.InvalidArgument : BridgeStatus.ManagedException);
        }
    }

    // Unroots an owned object. An unknown or already released handle, including one from another
    // component, is InvalidHandle.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int ReleaseObject(ulong token, ulong objectId, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (!ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated)) return (int)BridgeStatus.InvalidArgument;
        return (int)(ComponentRegistry.ReleaseObject(token, objectId) ? BridgeStatus.Success : BridgeStatus.InvalidHandle);
    }
}
