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
            return (int)StatusFor(exception);
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

    // Waits up to timeoutMs for a released component to be collected, running full blocking collections while it
    // waits. The state is Pending if the time ran out. Collection happens only when the host asks for it here.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int WaitForComponentUnload(ulong token, int timeoutMs, int* state, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (state != null) *state = -1;
        bool diagnosticValid = ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated);
        if (!diagnosticValid || state == null || timeoutMs < 0) return (int)BridgeStatus.InvalidArgument;
        try
        {
            if (!ComponentRegistry.TryWaitForUnload(token, timeoutMs, out ComponentUnloadState observed)) return (int)BridgeStatus.InvalidHandle;
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
            return (int)StatusFor(exception);
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
                ValueKinds.ValidateParameter(kinds[index]);
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
                    ManagedDiagnostic.Write(new MissingMethodException($"No public {(isStatic ? "static" : "instance")}, non-generic method '{methodName}' with the requested return and parameter types was found."),
                        new Span<byte>(diagnostic, capacity), out *written, out *truncated, report: false);
                    return (int)BridgeStatus.NotFound;
            }
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)StatusFor(exception);
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
            return (int)StatusFor(exception);
        }
    }

    // Registers a native struct mapping for a component (see ComponentRegistry.TryRegisterStruct for the
    // description format) and publishes its handle. NotFound (6): no such public struct. InvalidArgument (1):
    // the managed type is not blittable or does not match the native description; the diagnostic says how.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int RegisterStruct(ulong token, byte* description, int length, ulong* structId,
        byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (structId != null) *structId = 0;
        if (!ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated) || structId == null ||
            description == null || length <= 0)
            return (int)BridgeStatus.InvalidArgument;
        try
        {
            string text = new UTF8Encoding(false, true).GetString(new ReadOnlySpan<byte>(description, length));
            switch (ComponentRegistry.TryRegisterStruct(token, text, out ulong found))
            {
                case ComponentRegistry.FindResult.Found:
                    *structId = found;
                    return (int)BridgeStatus.Success;
                case ComponentRegistry.FindResult.InvalidHandle:
                    return (int)BridgeStatus.InvalidHandle;
                default:
                    ManagedDiagnostic.Write(new TypeLoadException("No public struct with that name was found."),
                        new Span<byte>(diagnostic, capacity), out *written, out *truncated, report: false);
                    return (int)BridgeStatus.NotFound;
            }
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)(exception is DecoderFallbackException ? BridgeStatus.InvalidArgument : StatusFor(exception));
        }
    }

    // Array exports. All validate the output buffers, report the component's exceptions as ManagedException and
    // bad input (a negative or out-of-range index, a wrong element type) as InvalidArgument; an unknown
    // component or object handle is InvalidHandle.
    private static unsafe int ArrayFailure(Exception exception, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
        return (int)StatusFor(exception);
    }

    // Creates an array of the element type in `kind` (see ValueKind.Array) and publishes a nonzero object
    // handle, released with ReleaseObject.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int CreateArray(ulong token, int kind, long length, ulong* objectId,
        byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (objectId != null) *objectId = 0;
        if (!ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated) || objectId == null)
            return (int)BridgeStatus.InvalidArgument;
        try
        {
            if (!ComponentRegistry.TryCreateArray(token, kind, length, out ulong id)) return (int)BridgeStatus.InvalidHandle;
            *objectId = id;
            return (int)BridgeStatus.Success;
        }
        catch (Exception exception) { return ArrayFailure(exception, diagnostic, capacity, written, truncated); }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int GetArrayLength(ulong token, ulong objectId, long* length,
        byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (length != null) *length = 0;
        if (!ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated) || length == null)
            return (int)BridgeStatus.InvalidArgument;
        try
        {
            if (!ComponentRegistry.TryGetArrayLength(token, objectId, out long value)) return (int)BridgeStatus.InvalidHandle;
            *length = value;
            return (int)BridgeStatus.Success;
        }
        catch (Exception exception) { return ArrayFailure(exception, diagnostic, capacity, written, truncated); }
    }

    // Reads element `index`. The slot holds a primitive's bits, or for a string or struct element a block (or
    // zero for a null string) that the caller copies and gives back with FreeString.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int GetArrayElement(ulong token, ulong objectId, int kind, long index, ulong* slot,
        byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (slot != null) *slot = 0;
        if (!ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated) || slot == null)
            return (int)BridgeStatus.InvalidArgument;
        try
        {
            if (!ComponentRegistry.TryGetArrayElement(token, objectId, kind, index, out ulong value)) return (int)BridgeStatus.InvalidHandle;
            *slot = value;
            return (int)BridgeStatus.Success;
        }
        catch (Exception exception) { return ArrayFailure(exception, diagnostic, capacity, written, truncated); }
    }

    // Writes element `index` from a slot in the layout GetArrayElement uses for input: bits, or a pointer to a
    // borrowed string or struct.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int SetArrayElement(ulong token, ulong objectId, int kind, long index, ulong slot,
        byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (!ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated))
            return (int)BridgeStatus.InvalidArgument;
        try
        {
            return ComponentRegistry.TrySetArrayElement(token, objectId, kind, index, slot) ? (int)BridgeStatus.Success : (int)BridgeStatus.InvalidHandle;
        }
        catch (Exception exception) { return ArrayFailure(exception, diagnostic, capacity, written, truncated); }
    }

    // Copies `count` primitive or struct elements from `start` to (intoArray 0) or from (intoArray 1) a caller buffer.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int CopyArray(ulong token, ulong objectId, int kind, long start, long count, void* buffer, int intoArray,
        byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (!ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated) || (intoArray != 0 && intoArray != 1))
            return (int)BridgeStatus.InvalidArgument;
        try
        {
            return ComponentRegistry.TryCopyArray(token, objectId, kind, start, count, buffer, intoArray == 1) ? (int)BridgeStatus.Success : (int)BridgeStatus.InvalidHandle;
        }
        catch (Exception exception) { return ArrayFailure(exception, diagnostic, capacity, written, truncated); }
    }

    // Registers a native function that component code can reach through a static function pointer field named
    // "Namespace.Type.Field". The signature is a return kind (Void, a sized number, or Pointer) and parameter
    // kinds. Registering the same name, signature and function again succeeds; a different one is
    // InvalidArgument. Process-wide: it applies to components loaded afterwards.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int RegisterInternalCall(byte* name, int nameLength, int returnKind, int* kinds, int count, void* function,
        byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (!ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated) || name == null || nameLength <= 0 ||
            count < 0 || count > 255 || (count > 0 && kinds == null) || function == null)
            return (int)BridgeStatus.InvalidArgument;
        try
        {
            InternalCalls.Register(new UTF8Encoding(false, true).GetString(new ReadOnlySpan<byte>(name, nameLength)), returnKind,
                new ReadOnlySpan<int>(kinds, count).ToArray(), (IntPtr)function);
            return (int)BridgeStatus.Success;
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)(exception is DecoderFallbackException ? BridgeStatus.InvalidArgument : StatusFor(exception));
        }
    }

    // Lists the component's public non-generic types as type handles, in declaration order. Publishes a block
    // (eight-byte length, then an int64 count and that many int64 handles) the caller gives back with FreeString.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int ListTypes(ulong token, ulong* block, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (block != null) *block = 0;
        if (!ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated) || block == null)
            return (int)BridgeStatus.InvalidArgument;
        try
        {
            if (!ComponentRegistry.TryListTypes(token, out ulong[] ids)) return (int)BridgeStatus.InvalidHandle;
            var data = new byte[8 + ids.Length * 8];
            BitConverter.TryWriteBytes(data.AsSpan(0, 8), (long)ids.Length);
            for (int index = 0; index < ids.Length; ++index) BitConverter.TryWriteBytes(data.AsSpan(8 + index * 8, 8), (long)ids[index]);
            *block = TypeQueries.ToBlock(data);
            return (int)BridgeStatus.Success;
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)StatusFor(exception);
        }
    }

    // A snapshot of a type: query 0 base type, 1 interfaces, 2 custom attributes, 3 fields, 4 properties, 5 methods (see TypeQueries for the
    // format). Publishes a block the caller gives back with FreeString. No component code runs.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int QueryType(ulong token, ulong typeId, int query, ulong* block,
        byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (block != null) *block = 0;
        if (!ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated) || block == null)
            return (int)BridgeStatus.InvalidArgument;
        try
        {
            if (query < 0 || query > 5) throw new BridgeArgumentException($"Unknown type query {query}.");
            if (!ComponentRegistry.TryQueryType(token, typeId, (TypeQueries.Query)query, out byte[] data)) return (int)BridgeStatus.InvalidHandle;
            *block = TypeQueries.ToBlock(data);
            return (int)BridgeStatus.Success;
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)StatusFor(exception);
        }
    }

    // Whether the type is assignable to another: `otherId` is a type handle of this component, or 0 with the
    // target named in explicit-length UTF-8. *result is 1 or 0. NotFound (6): the named type cannot be resolved.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int IsTypeAssignable(ulong token, ulong typeId, ulong otherId, byte* name, int nameLength, int* result,
        byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (result != null) *result = 0;
        if (!ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated) || result == null ||
            (otherId == 0) == (name == null) || (name != null && nameLength <= 0))
            return (int)BridgeStatus.InvalidArgument;
        try
        {
            string? targetName = name == null ? null : new UTF8Encoding(false, true).GetString(new ReadOnlySpan<byte>(name, nameLength));
            if (targetName != null && targetName.Contains('\0')) throw new BridgeArgumentException("A type name cannot contain null characters.");
            switch (ComponentRegistry.TryIsAssignable(token, typeId, otherId, targetName))
            {
                case ComponentRegistry.AssignResult.Assignable:
                    *result = 1;
                    return (int)BridgeStatus.Success;
                case ComponentRegistry.AssignResult.NotAssignable:
                    return (int)BridgeStatus.Success;
                case ComponentRegistry.AssignResult.InvalidHandle:
                    return (int)BridgeStatus.InvalidHandle;
                default:
                    ManagedDiagnostic.Write(new TypeLoadException($"No type named '{targetName}' could be resolved from the component."),
                        new Span<byte>(diagnostic, capacity), out *written, out *truncated, report: false);
                    return (int)BridgeStatus.NotFound;
            }
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)(exception is DecoderFallbackException ? BridgeStatus.InvalidArgument : StatusFor(exception));
        }
    }

    // Gives back a string block returned in a result slot. Zero is ignored. Cannot fail.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static void FreeString(ulong block)
    {
        if (block != 0) ValueKinds.FreeString(block);
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
            return (int)StatusFor(exception);
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
                ValueKinds.ValidateParameter(kinds[index]);
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
            return (int)StatusFor(exception);
        }
    }

    // Unroots an owned object. An unknown or already released handle, including one from another
    // component, is InvalidHandle.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int ReleaseObject(ulong token, ulong objectId, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (!ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated)) return (int)BridgeStatus.InvalidArgument;
        try
        {
            return (int)(ComponentRegistry.ReleaseObject(token, objectId) ? BridgeStatus.Success : BridgeStatus.InvalidHandle);
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)BridgeStatus.ManagedException;
        }
    }

    // Finds a public field (memberKind 0) or property (1) by name and value kind (see ValueKind; Void is
    // not a member type). Success publishes a nonzero handle; NotFound (6): no such public member of that
    // type, an indexer, or a property with no public accessor.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int FindMember(ulong token, ulong typeId, byte* name, int nameLength, int memberKind, int valueKind,
        ulong* memberId, byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (memberId != null) *memberId = 0;
        if (!ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated) || memberId == null ||
            name == null || nameLength <= 0 || (memberKind != 0 && memberKind != 1))
            return (int)BridgeStatus.InvalidArgument;
        string memberName;
        try
        {
            ValueKinds.ValidateParameter(valueKind);
            memberName = new UTF8Encoding(false, true).GetString(new ReadOnlySpan<byte>(name, nameLength));
            if (memberName.Contains('\0')) throw new BridgeArgumentException("A member name cannot contain null characters.");
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)(exception is DecoderFallbackException or BridgeArgumentException ? BridgeStatus.InvalidArgument : BridgeStatus.ManagedException);
        }
        try
        {
            switch (ComponentRegistry.TryFindMember(token, typeId, memberName, memberKind, valueKind, out ulong found))
            {
                case ComponentRegistry.FindResult.Found:
                    *memberId = found;
                    return (int)BridgeStatus.Success;
                case ComponentRegistry.FindResult.InvalidHandle:
                    return (int)BridgeStatus.InvalidHandle;
                default:
                    ManagedDiagnostic.Write(new MissingMemberException($"No public {(memberKind == 0 ? "field" : "property")} '{memberName}' of the requested type was found."),
                        new Span<byte>(diagnostic, capacity), out *written, out *truncated, report: false);
                    return (int)BridgeStatus.NotFound;
            }
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)StatusFor(exception);
        }
    }

    // Reads a member. objectId is zero for a static member and an owned object for an instance member;
    // using the wrong form is InvalidArgument. An exception thrown by a property getter is ManagedException.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int GetMember(ulong token, ulong memberId, ulong objectId, ulong* value,
        byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (value != null) *value = 0;
        if (!ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated) || value == null)
            return (int)BridgeStatus.InvalidArgument;
        try
        {
            if (!ComponentRegistry.TryGetMemberValue(token, memberId, objectId, out ulong read)) return (int)BridgeStatus.InvalidHandle;
            *value = read;
            return (int)BridgeStatus.Success;
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)StatusFor(exception);
        }
    }

    // Writes a member, with the same rules as GetMember. A read-only field or property is InvalidArgument.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    internal static unsafe int SetMember(ulong token, ulong memberId, ulong objectId, ulong value,
        byte* diagnostic, int capacity, int* written, int* truncated)
    {
        if (!ManagedDiagnostic.ValidateOutput(diagnostic, capacity, written, truncated)) return (int)BridgeStatus.InvalidArgument;
        try
        {
            return (int)(ComponentRegistry.TrySetMemberValue(token, memberId, objectId, value) ? BridgeStatus.Success : BridgeStatus.InvalidHandle);
        }
        catch (Exception exception)
        {
            ManagedDiagnostic.Write(exception, new Span<byte>(diagnostic, capacity), out *written, out *truncated);
            return (int)StatusFor(exception);
        }
    }

    // Bridge validation is InvalidArgument and a stale handle is InvalidHandle; anything else, including
    // every exception from component code, is ManagedException.
    private static BridgeStatus StatusFor(Exception exception) => exception switch
    {
        BridgeArgumentException => BridgeStatus.InvalidArgument,
        BridgeHandleException => BridgeStatus.InvalidHandle,
        _ => BridgeStatus.ManagedException
    };
}
