using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;

namespace Iris.Bridge;

internal static class ComponentRegistry
{
    // A method resolved by exact signature, with the wire kinds of its parameters and return value.
    private sealed record MethodRecord(MethodInfo Method, int[] ParameterKinds, int ReturnKind, bool IsStatic);

    // Handles to types and methods looked up in one component. They live in the active entry, so
    // releasing the component drops every Type and MethodInfo reference with it; no handle outlives
    // its component.
    private sealed class HandleTable
    {
        internal readonly Dictionary<Type, ulong> TypeIds = new();
        internal readonly Dictionary<ulong, Type> Types = new();
        internal readonly Dictionary<MethodInfo, ulong> MethodIds = new();
        internal readonly Dictionary<ulong, MethodRecord> Methods = new();
        // Objects the native side owns. These are the only strong references to them, so releasing the
        // component (which drops the entry) or the object (which removes it here) is what unroots them.
        internal readonly Dictionary<ulong, object> Objects = new();
    }
    private sealed record Entry(ComponentLoadContext Context, Assembly Assembly, string Name)
    {
        internal HandleTable Handles { get; } = new();
    }
    private sealed class Registration(Entry entry)
    {
        internal Entry? Active = entry;
        internal readonly WeakReference Context = new(entry.Context, trackResurrection: true);
    }
    private static readonly object Gate = new();
    private static readonly Dictionary<ulong, Registration> Registrations = new();
    private static ulong lastToken;
    private static ulong lastTypeId; // Type and method ids come from process-wide counters,
    private static ulong lastMethodId; // so a handle is never reused.
    private static ulong lastObjectId;

    internal enum FindResult { Found, NotFound, InvalidHandle }
    internal enum ReleaseResult { Released, InvalidHandle, InUse }

    // Internal verification seam; never a native/public diagnostic API.
    internal static int Count
    {
        get
        {
            lock (Gate)
            {
                int count = 0;
                foreach (Registration record in Registrations.Values) if (record.Active != null) ++count;
                return count;
            }
        }
    }
    internal static int TrackingCount { get { lock (Gate) return Registrations.Count; } }

    internal static ulong Load(string path)
    {
        if (!Path.IsPathFullyQualified(path) || path.Contains('\0'))
            throw new BridgeArgumentException("A component path must be absolute and contain no null characters.");
        if (SharedAssemblies.IsShared(AssemblyName.GetAssemblyName(path).Name))
            throw new BridgeArgumentException("A shared assembly, such as the bridge, cannot be loaded as a collectible component.");

        // Resolver creation can fail. Do it before creating a collectible context.
        var resolver = new AssemblyDependencyResolver(path);
        var context = new ComponentLoadContext(resolver);
        bool registered = false;
        try
        {
            Assembly assembly = context.LoadFromAssemblyPath(path);
            if (!context.IsCollectible || !assembly.IsCollectible || AssemblyLoadContext.GetLoadContext(assembly) != context)
                throw new InvalidOperationException("Component loading did not produce a collectible assembly in its own context.");
            string name = assembly.GetName().Name ?? throw new InvalidOperationException("Assembly has no name.");
            var record = new Registration(new Entry(context, assembly, name));
            ulong token;
            lock (Gate)
            {
                token = checked(++lastToken); // Never reuse IDs, even after a failed registration.
                Registrations.Add(token, record);
                registered = true;
            }
            BridgeLog.Write(LogLevel.Info, $"Loaded component '{name}' (handle {token}).");
            return token;
        }
        finally
        {
            if (!registered) context.Unload();
        }
    }

    internal static string? GetName(ulong token)
    {
        lock (Gate) return Registrations.TryGetValue(token, out Registration? record) ? record.Active?.Name : null;
    }

    // Looks up a public, closed type defined in the component assembly and returns its handle. Looking
    // up the same type again returns the same handle.
    internal static FindResult TryFindType(ulong token, string typeName, out ulong typeId)
    {
        typeId = 0;
        Entry entry;
        lock (Gate)
        {
            if (!Registrations.TryGetValue(token, out Registration? record) || record.Active is not Entry active)
                return FindResult.InvalidHandle;
            entry = active;
        }
        // No registry lock is held while resolving a type, which can load assemblies.
        Type? type = entry.Assembly.GetType(typeName, throwOnError: false, ignoreCase: false);
        if (type == null || type.Assembly != entry.Assembly || !type.IsVisible || type.ContainsGenericParameters)
            return FindResult.NotFound;
        lock (Gate)
        {
            // The component may have been released while the type was resolved.
            if (!Registrations.TryGetValue(token, out Registration? record) || !ReferenceEquals(record.Active, entry))
                return FindResult.InvalidHandle;
            if (!entry.Handles.TypeIds.TryGetValue(type, out typeId))
            {
                typeId = checked(++lastTypeId);
                entry.Handles.TypeIds.Add(type, typeId);
                entry.Handles.Types.Add(typeId, type);
            }
            return FindResult.Found;
        }
    }

    // field 0 is the type's full name, 1 its assembly's simple name. False for an unknown component or
    // type handle, including a handle that belongs to another component.
    internal static bool TryGetTypeName(ulong token, ulong typeId, int field, out string? text)
    {
        text = null;
        if (field != 0 && field != 1) throw new BridgeArgumentException("Unknown type name field.");
        lock (Gate)
        {
            if (!Registrations.TryGetValue(token, out Registration? record) || record.Active is not Entry entry ||
                !entry.Handles.Types.TryGetValue(typeId, out Type? type)) return false;
            text = field == 0 ? type.FullName ?? type.Name : type.Assembly.GetName().Name ?? "";
            return true;
        }
    }

    // Finds a public static, non-generic method on a looked-up type by name and exact parameter and
    // return kinds. Looking the same method up again returns the same handle. UnmanagedCallersOnly
    // methods are excluded.
    internal static FindResult TryFindMethod(ulong token, ulong typeId, string name, int returnKind, int[] parameterKinds, bool isStatic, out ulong methodId)
    {
        methodId = 0;
        Entry entry;
        Type type;
        lock (Gate)
        {
            if (!Registrations.TryGetValue(token, out Registration? record) || record.Active is not Entry active ||
                !active.Handles.Types.TryGetValue(typeId, out Type? found)) return FindResult.InvalidHandle;
            entry = active;
            type = found;
        }
        Type returnType = ValueKinds.ToType(returnKind, allowVoid: true);
        var parameterTypes = new Type[parameterKinds.Length];
        for (int index = 0; index < parameterTypes.Length; ++index)
            parameterTypes[index] = ValueKinds.ToType(parameterKinds[index], allowVoid: false);

        // No registry lock is held while reflecting, which can load assemblies.
        // Static methods must be declared on the type; instance methods may be inherited, and a call
        // through a base type's method dispatches virtually to the object's override.
        BindingFlags flags = isStatic ? BindingFlags.Public | BindingFlags.Static | BindingFlags.DeclaredOnly
                                      : BindingFlags.Public | BindingFlags.Instance;
        MethodInfo? method = type.GetMethod(name, flags, binder: null, types: parameterTypes, modifiers: null);
        if (method == null || method.ReturnType != returnType || method.IsGenericMethod ||
            method.IsDefined(typeof(System.Runtime.InteropServices.UnmanagedCallersOnlyAttribute), inherit: false))
            return FindResult.NotFound;
        // The default binder accepts widening conversions (an int argument for a long parameter), but
        // the native types name the managed signature exactly, so every parameter must match.
        ParameterInfo[] declared = method.GetParameters();
        for (int index = 0; index < declared.Length; ++index)
            if (declared[index].ParameterType != parameterTypes[index]) return FindResult.NotFound;
        lock (Gate)
        {
            if (!Registrations.TryGetValue(token, out Registration? record) || !ReferenceEquals(record.Active, entry))
                return FindResult.InvalidHandle;
            if (!entry.Handles.MethodIds.TryGetValue(method, out methodId))
            {
                methodId = checked(++lastMethodId);
                entry.Handles.MethodIds.Add(method, methodId);
                entry.Handles.Methods.Add(methodId, new MethodRecord(method, (int[])parameterKinds.Clone(), returnKind, isStatic));
            }
            return FindResult.Found;
        }
    }

    // Runs a found static method. False for an unknown component or method handle. Throws
    // BridgeArgumentException for a wrong argument count or an instance method; component exceptions
    // propagate unwrapped.
    internal static bool TryInvokeMethod(ulong token, ulong methodId, ReadOnlySpan<ulong> arguments, out ulong result)
    {
        result = 0;
        if (!TryGetMethod(token, methodId, requireObject: false, 0, out MethodRecord? method, out _)) return false;
        if (!method!.IsStatic) throw new BridgeArgumentException("An instance method needs an object.");
        result = Run(method, null, arguments);
        return true;
    }

    // Runs a found instance method on an owned object. False for an unknown component, method or object
    // handle. Throws BridgeArgumentException for a static method, an object that is not an instance of
    // the method's declaring type, or a wrong argument count.
    internal static bool TryInvokeInstanceMethod(ulong token, ulong methodId, ulong objectId, ReadOnlySpan<ulong> arguments, out ulong result)
    {
        result = 0;
        if (!TryGetMethod(token, methodId, requireObject: true, objectId, out MethodRecord? method, out object? target)) return false;
        if (method!.IsStatic) throw new BridgeArgumentException("A static method cannot be invoked on an object.");
        if (!method.Method.DeclaringType!.IsInstanceOfType(target))
            throw new BridgeArgumentException("The object is not an instance of the method's declaring type.");
        result = Run(method, target, arguments);
        return true;
    }

    private static bool TryGetMethod(ulong token, ulong methodId, bool requireObject, ulong objectId, out MethodRecord? method, out object? target)
    {
        method = null;
        target = null;
        lock (Gate)
        {
            if (!Registrations.TryGetValue(token, out Registration? record) || record.Active is not Entry entry ||
                !entry.Handles.Methods.TryGetValue(methodId, out MethodRecord? found)) return false;
            if (requireObject && !entry.Handles.Objects.TryGetValue(objectId, out target)) return false;
            method = found;
            return true;
        }
    }

    private static ulong Run(MethodRecord method, object? target, ReadOnlySpan<ulong> arguments)
    {
        if (arguments.Length != method.ParameterKinds.Length)
            throw new BridgeArgumentException($"The method takes {method.ParameterKinds.Length} arguments, not {arguments.Length}.");
        var values = new object[arguments.Length];
        for (int index = 0; index < values.Length; ++index)
            values[index] = ValueKinds.ToObject(method.ParameterKinds[index], arguments[index]);
        // No registry lock is held while user code runs. The locals root the method and target for the call.
        object? returned = method.Method.Invoke(target, BindingFlags.DoNotWrapExceptions, binder: null, values, culture: null);
        return ValueKinds.ToSlot(method.ReturnKind, returned);
    }

    internal enum CreateResult { Created, NotFound, InvalidHandle }

    // Constructs an object of a looked-up type with the public constructor whose parameters match the
    // kinds exactly, and roots it in the component. A constructor exception propagates unwrapped and
    // roots nothing.
    internal static CreateResult TryCreateObject(ulong token, ulong typeId, int[] parameterKinds, ReadOnlySpan<ulong> arguments, out ulong objectId)
    {
        objectId = 0;
        Entry entry;
        Type type;
        lock (Gate)
        {
            if (!Registrations.TryGetValue(token, out Registration? record) || record.Active is not Entry active ||
                !active.Handles.Types.TryGetValue(typeId, out Type? found)) return CreateResult.InvalidHandle;
            entry = active;
            type = found;
        }
        var parameterTypes = new Type[parameterKinds.Length];
        for (int index = 0; index < parameterTypes.Length; ++index)
            parameterTypes[index] = ValueKinds.ToType(parameterKinds[index], allowVoid: false);
        if (type.IsAbstract || type.IsInterface) return CreateResult.NotFound;
        ConstructorInfo? constructor = type.GetConstructor(BindingFlags.Public | BindingFlags.Instance, binder: null, types: parameterTypes, modifiers: null);
        if (constructor == null) return CreateResult.NotFound;
        ParameterInfo[] declared = constructor.GetParameters();
        for (int index = 0; index < declared.Length; ++index)
            if (declared[index].ParameterType != parameterTypes[index]) return CreateResult.NotFound;
        var values = new object[arguments.Length];
        for (int index = 0; index < values.Length; ++index)
            values[index] = ValueKinds.ToObject(parameterKinds[index], arguments[index]);

        // No registry lock is held while the constructor runs.
        object instance = constructor.Invoke(BindingFlags.DoNotWrapExceptions, binder: null, values, culture: null);
        lock (Gate)
        {
            // The component may have been released while the constructor ran; then the object is dropped.
            if (!Registrations.TryGetValue(token, out Registration? record) || !ReferenceEquals(record.Active, entry))
                return CreateResult.InvalidHandle;
            objectId = checked(++lastObjectId);
            entry.Handles.Objects.Add(objectId, instance);
            return CreateResult.Created;
        }
    }

    // Unroots an owned object. False for an unknown component or object, including one already released.
    internal static bool ReleaseObject(ulong token, ulong objectId)
    {
        lock (Gate)
        {
            return Registrations.TryGetValue(token, out Registration? record) && record.Active is Entry entry &&
                   entry.Handles.Objects.Remove(objectId);
        }
    }

    // Verification seams, never native-visible.
    internal static int LiveObjectCount(ulong token)
    {
        lock (Gate) return Registrations.TryGetValue(token, out Registration? record) && record.Active is Entry entry ? entry.Handles.Objects.Count : 0;
    }

    internal static object? PeekObject(ulong token, ulong objectId)
    {
        lock (Gate)
        {
            return Registrations.TryGetValue(token, out Registration? record) && record.Active is Entry entry &&
                   entry.Handles.Objects.TryGetValue(objectId, out object? target) ? target : null;
        }
    }

    // Releases a component. Unless forced, a component with live objects is InUse and stays loaded:
    // dropping its roots would silently invalidate objects native code still owns. Forcing drops them.
    internal static ReleaseResult TryRelease(ulong token, bool force)
    {
        Entry? entry;
        lock (Gate)
        {
            if (!Registrations.TryGetValue(token, out Registration? record) || record.Active == null) return ReleaseResult.InvalidHandle;
            if (!force && record.Active.Handles.Objects.Count > 0) return ReleaseResult.InUse;
            entry = record.Active;
            // Invalidate access and drop all strong fields before running unload handlers.
            record.Active = null;
        }
        // No permanent registry root remains, even if an unload handler throws.
        BridgeLog.Write(LogLevel.Info, $"Releasing component '{entry.Name}' (handle {token}); unloading is cooperative.");
        entry.Context.Unload();
        return ReleaseResult.Released;
    }

    internal static bool Release(ulong token) => TryRelease(token, force: false) == ReleaseResult.Released;

    internal static bool TryGetUnloadState(ulong token, out ComponentUnloadState state)
    {
        state = default;
        lock (Gate)
        {
            if (!Registrations.TryGetValue(token, out Registration? record)) return false;
            state = record.Active != null ? ComponentUnloadState.NotRequested :
                record.Context.IsAlive ? ComponentUnloadState.Pending : ComponentUnloadState.Collected;
            return true;
        }
    }

    internal static bool ForgetUnload(ulong token)
    {
        lock (Gate)
        {
            // Forgetting must never remove a live registration/its strong roots.
            return Registrations.TryGetValue(token, out Registration? record) && record.Active == null && Registrations.Remove(token);
        }
    }
}
