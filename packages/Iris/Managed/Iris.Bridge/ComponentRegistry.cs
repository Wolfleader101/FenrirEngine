using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;

namespace Iris.Bridge;

internal static class ComponentRegistry
{
    // A method resolved by exact signature, with the wire kinds of its parameters and return value.
    private sealed record MethodRecord(MethodInfo Method, int[] ParameterKinds, Type[] ParameterTypes, int ReturnKind, bool IsStatic);

    // A public field or property of a looked-up type. Properties go through their public accessors, so an
    // exception thrown by one is the component's own and is not wrapped.
    private sealed record MemberRecord(FieldInfo? Field, PropertyInfo? Property, MethodInfo? Getter, MethodInfo? Setter,
        Type ValueType, int ValueKind, bool IsStatic, Type DeclaringType);

    // A struct or enum registered from native: its managed type and size in bytes. For a struct, BoolOffsets
    // lists where its bool bytes are (nested structs and inline arrays included), so a copy from native can
    // make them 0 or 1. For an enum, UnderlyingKind is the wire kind of its integer type (zero for a struct).
    private sealed record StructRecord(Type Type, int Size, int[] BoolOffsets, int UnderlyingKind = 0);

    // Handles to types, methods and members looked up in one component. They live in the active entry, so
    // releasing the component drops every Type and MethodInfo reference with it; no handle outlives
    // its component.

    private sealed class HandleTable
    {
        internal readonly Dictionary<Type, ulong> StructIds = new();
        internal readonly Dictionary<ulong, StructRecord> Structs = new();
        internal readonly Dictionary<Type, ulong> TypeIds = new();
        internal readonly Dictionary<ulong, Type> Types = new();
        internal readonly Dictionary<MethodInfo, ulong> MethodIds = new();
        internal readonly Dictionary<ulong, MethodRecord> Methods = new();
        internal readonly Dictionary<MemberInfo, ulong> MemberIds = new();
        internal readonly Dictionary<ulong, MemberRecord> Members = new();
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
    private static ulong lastMemberId;
    private static ulong lastStructId;

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
            // Bind internal calls before registering, so a component that cannot be bound is never visible.
            InternalCalls.Bind(assembly);
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

    // The handle for a type of this component, created on first use. Caller holds no lock.
    private static ulong TypeIdFor(Entry entry, Type type)
    {
        lock (Gate)
        {
            if (!entry.Handles.TypeIds.TryGetValue(type, out ulong typeId))
            {
                typeId = checked(++lastTypeId);
                entry.Handles.TypeIds.Add(type, typeId);
                entry.Handles.Types.Add(typeId, type);
            }
            return typeId;
        }
    }

    // The public, non-generic types the component assembly defines, in declaration order (metadata token),
    // nested public types included. These are exactly the types FindType can find. False for an unknown component.
    internal static bool TryListTypes(ulong token, out ulong[] typeIds)
    {
        typeIds = Array.Empty<ulong>();
        Entry entry;
        lock (Gate)
        {
            if (!Registrations.TryGetValue(token, out Registration? record) || record.Active is not Entry active) return false;
            entry = active;
        }
        Type[] types;
        try { types = entry.Assembly.GetTypes(); }
        catch (ReflectionTypeLoadException exception)
        {
            // A type that cannot load (a base class in a missing assembly, say) is left out, and the host is told.
            types = Array.FindAll(exception.Types, type => type != null)!;
            BridgeLog.Write(LogLevel.Warning, $"{exception.LoaderExceptions.Length} type(s) of component '{entry.Name}' could not be loaded and are not listed: {exception.LoaderExceptions.FirstOrDefault()?.Message}");
        }
        Array.Sort(types, (left, right) => left.MetadataToken.CompareTo(right.MetadataToken));
        var ids = new List<ulong>();
        foreach (Type type in types)
        {
            if (!type.IsVisible || type.ContainsGenericParameters) continue;
            lock (Gate)
            {
                if (!Registrations.TryGetValue(token, out Registration? record) || !ReferenceEquals(record.Active, entry)) return false;
            }
            ids.Add(TypeIdFor(entry, type));
        }
        typeIds = ids.ToArray();
        return true;
    }

    // A snapshot (see TypeQueries) of a looked-up type. False for an unknown component or type handle.
    internal static bool TryQueryType(ulong token, ulong typeId, TypeQueries.Query query, out byte[] data)
    {
        data = Array.Empty<byte>();
        Type type;
        lock (Gate)
        {
            if (!Registrations.TryGetValue(token, out Registration? record) || record.Active is not Entry active ||
                !active.Handles.Types.TryGetValue(typeId, out Type? found)) return false;
            type = found;
        }
        data = TypeQueries.Run(type, query);
        return true;
    }

    internal enum AssignResult { Assignable, NotAssignable, InvalidHandle, UnknownType }

    // Whether an instance of the type can be stored where `target` is expected: the target is another handle
    // of this component (otherId), or a type named in the component assembly, the framework, or an assembly
    // the component references (a contract, shared or private). A name nothing resolves is UnknownType.
    internal static AssignResult TryIsAssignable(ulong token, ulong typeId, ulong otherId, string? targetName)
    {
        Entry entry;
        Type type, target;
        lock (Gate)
        {
            if (!Registrations.TryGetValue(token, out Registration? record) || record.Active is not Entry active ||
                !active.Handles.Types.TryGetValue(typeId, out Type? found)) return AssignResult.InvalidHandle;
            entry = active;
            type = found;
            if (targetName == null)
            {
                if (!active.Handles.Types.TryGetValue(otherId, out Type? other)) return AssignResult.InvalidHandle;
                target = other;
            }
            else
            {
                target = null!;
            }
        }
        if (targetName != null)
        {
            Type? resolved = entry.Assembly.GetType(targetName, throwOnError: false, ignoreCase: false);
            if (resolved != null && !resolved.IsVisible) resolved = null; // Only public types, as everywhere else.
            resolved ??= Type.GetType(targetName, throwOnError: false);
            if (resolved == null)
            {
                foreach (AssemblyName reference in entry.Assembly.GetReferencedAssemblies())
                {
                    try { resolved = entry.Context.LoadFromAssemblyName(reference).GetType(targetName, throwOnError: false, ignoreCase: false); }
                    catch (Exception exception) when (exception is IOException or BadImageFormatException or FileLoadException or InvalidOperationException) { }
                    if (resolved != null) break;
                }
            }
            if (resolved == null) return AssignResult.UnknownType;
            lock (Gate)
            {
                // The component may have been released while the name was resolved.
                if (!Registrations.TryGetValue(token, out Registration? record) || !ReferenceEquals(record.Active, entry)) return AssignResult.InvalidHandle;
            }
            target = resolved;
        }
        return target.IsAssignableFrom(type) ? AssignResult.Assignable : AssignResult.NotAssignable;
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

    // Class and interface types an object handle can stand for. string and arrays are separate kinds.
    private static bool IsObjectType(Type type) =>
        (type.IsClass || type.IsInterface) && type != typeof(string) && !type.IsArray && !type.IsByRef && !type.IsPointer &&
        !type.ContainsGenericParameters;

    private static StructRecord StructOf(Entry entry, int kind)
    {
        lock (Gate)
        {
            if (!entry.Handles.Structs.TryGetValue(ValueKinds.StructId(kind), out StructRecord? record))
                throw new BridgeArgumentException("A struct kind does not name a struct registered for this component.");
            return record;
        }
    }

    // Every struct kind a lookup mentions must be registered for this component, whatever else about the
    // lookup would match; a foreign or unknown struct handle is an argument error, not a quiet NotFound.
    private static void RequireStructs(Entry entry, int returnKind, int[] parameterKinds)
    {
        RequireStruct(entry, returnKind);
        foreach (int kind in parameterKinds) RequireStruct(entry, kind);
    }

    private static void RequireStruct(Entry entry, int kind)
    {
        if (ValueKinds.IsRegistered(kind) || (ValueKinds.IsArray(kind) && (kind & 0xFF) == (int)ValueKind.StructArray))
        {
            // An array of enums is not supported, so a struct array must name a struct.
            if (StructOf(entry, kind).UnderlyingKind != 0 && !ValueKinds.IsEnum(kind))
                throw new BridgeArgumentException("An array of enums is not supported.");
        }
    }

    // The managed element type of an array kind.
    private static Type ElementTypeOf(Entry entry, int arrayKind) =>
        (arrayKind & 0xFF) == (int)ValueKind.StructArray ? StructOf(entry, arrayKind).Type : ValueKinds.ToType(arrayKind >> 8, allowVoid: false);

    private static bool KindMatches(Entry entry, Type type, int kind, bool isReturn)
    {
        if (ValueKinds.IsRegistered(kind)) return type == StructOf(entry, kind).Type;
        if (ValueKinds.IsArray(kind)) return type.IsSZArray && type.GetElementType() == ElementTypeOf(entry, kind);
        switch ((ValueKind)kind)
        {
            case ValueKind.Object: return IsObjectType(type);
            case ValueKind.Void: return isReturn && type == typeof(void);
            default: return type == ValueKinds.ToType(kind, allowVoid: false);
        }
    }

    private static bool SameParameters(MethodBase first, MethodBase second)
    {
        ParameterInfo[] left = first.GetParameters(), right = second.GetParameters();
        if (left.Length != right.Length) return false;
        for (int index = 0; index < left.Length; ++index)
            if (left[index].ParameterType != right[index].ParameterType) return false;
        return true;
    }

    private static bool SignatureMatches(Entry entry, ParameterInfo[] parameters, int[] kinds)
    {
        if (parameters.Length != kinds.Length) return false;
        for (int index = 0; index < kinds.Length; ++index)
            if (!KindMatches(entry, parameters[index].ParameterType, kinds[index], isReturn: false)) return false;
        return true;
    }

    // Finds a public, non-generic method on a looked-up type by name and signature: by-value kinds must
    // match exactly, and an Object kind matches any class or interface type. Static methods must be
    // declared on the type; instance methods may be inherited, and a call through a base type's method
    // dispatches virtually. UnmanagedCallersOnly methods are excluded. Looking the same method up again
    // returns the same handle. A signature that matches more than one overload is ambiguous.
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
        RequireStructs(entry, returnKind, parameterKinds);
        BindingFlags flags = isStatic ? BindingFlags.Public | BindingFlags.Static | BindingFlags.DeclaredOnly
                                      : BindingFlags.Public | BindingFlags.Instance;
        // No registry lock is held while reflecting, which can load assemblies.
        MethodInfo? method = null;
        foreach (MethodInfo candidate in type.GetMethods(flags))
        {
            if (candidate.Name != name || candidate.IsGenericMethod ||
                candidate.IsDefined(typeof(System.Runtime.InteropServices.UnmanagedCallersOnlyAttribute), inherit: false) ||
                !KindMatches(entry, candidate.ReturnType, returnKind, isReturn: true) || !SignatureMatches(entry, candidate.GetParameters(), parameterKinds))
                continue;
            if (method != null)
            {
                if (!SameParameters(method, candidate))
                    throw new BridgeArgumentException($"The signature matches more than one overload of '{name}'; object parameters match any class, so overloads that differ only by them are ambiguous.");
                // The same signature declared on a base and a derived type is `new` hiding: take the derived one.
                if (!candidate.DeclaringType!.IsSubclassOf(method.DeclaringType!)) continue;
            }
            method = candidate;
        }
        if (method == null) return FindResult.NotFound;
        lock (Gate)
        {
            if (!Registrations.TryGetValue(token, out Registration? record) || !ReferenceEquals(record.Active, entry))
                return FindResult.InvalidHandle;
            if (!entry.Handles.MethodIds.TryGetValue(method, out methodId))
            {
                methodId = checked(++lastMethodId);
                Type[] parameterTypes = Array.ConvertAll(method.GetParameters(), parameter => parameter.ParameterType);
                entry.Handles.MethodIds.Add(method, methodId);
                entry.Handles.Methods.Add(methodId, new MethodRecord(method, (int[])parameterKinds.Clone(), parameterTypes, returnKind, isStatic));
            }
            return FindResult.Found;
        }
    }

    // Runs a found static method. False for an unknown component or method handle. Throws
    // BridgeArgumentException for a wrong argument count, an instance method or an object argument of the
    // wrong type, and BridgeHandleException for an object argument that is not held; component
    // exceptions propagate unwrapped.
    internal static bool TryInvokeMethod(ulong token, ulong methodId, ReadOnlySpan<ulong> arguments, out ulong result)
    {
        result = 0;
        if (!TryGetMethod(token, methodId, requireObject: false, 0, out Entry? entry, out MethodRecord? method, out _)) return false;
        if (!method!.IsStatic) throw new BridgeArgumentException("An instance method needs an object.");
        result = Run(token, entry!, method, null, arguments);
        return true;
    }

    // Runs a found instance method on an owned object. False for an unknown component, method or object
    // handle. Throws BridgeArgumentException for a static method, an object that is not an instance of
    // the method's declaring type, or a wrong argument count.
    internal static bool TryInvokeInstanceMethod(ulong token, ulong methodId, ulong objectId, ReadOnlySpan<ulong> arguments, out ulong result)
    {
        result = 0;
        if (!TryGetMethod(token, methodId, requireObject: true, objectId, out Entry? entry, out MethodRecord? method, out object? target)) return false;
        if (method!.IsStatic) throw new BridgeArgumentException("A static method cannot be invoked on an object.");
        if (!method.Method.DeclaringType!.IsInstanceOfType(target))
            throw new BridgeArgumentException("The object is not an instance of the method's declaring type.");
        result = Run(token, entry!, method, target, arguments);
        return true;
    }

    private static bool TryGetMethod(ulong token, ulong methodId, bool requireObject, ulong objectId,
        out Entry? entry, out MethodRecord? method, out object? target)
    {
        entry = null;
        method = null;
        target = null;
        lock (Gate)
        {
            if (!Registrations.TryGetValue(token, out Registration? record) || record.Active is not Entry active ||
                !active.Handles.Methods.TryGetValue(methodId, out MethodRecord? found)) return false;
            if (requireObject && !active.Handles.Objects.TryGetValue(objectId, out target)) return false;
            entry = active;
            method = found;
            return true;
        }
    }

    // Turns wire slots into managed arguments. An Object slot is looked up in the component's table and
    // must be an instance of the parameter type, which is the only place the real type can be checked.
    private static object?[] ConvertArguments(Entry entry, int[] kinds, Type[] types, ReadOnlySpan<ulong> arguments)
    {
        var values = new object?[arguments.Length];
        for (int index = 0; index < values.Length; ++index)
        {
            if (ValueKinds.IsStruct(kinds[index]))
            {
                values[index] = StructFromSlot(entry, kinds[index], arguments[index]);
                continue;
            }
            if (ValueKinds.IsEnum(kinds[index]))
            {
                values[index] = EnumFromSlot(entry, kinds[index], arguments[index]);
                continue;
            }
            if (ValueKinds.IsArray(kinds[index]))
            {
                // An array travels as an object handle; zero is a null array.
                object? array = null;
                if (arguments[index] != 0)
                {
                    lock (Gate)
                    {
                        if (!entry.Handles.Objects.TryGetValue(arguments[index], out array))
                            throw new BridgeHandleException($"Argument {index} is not an array this component holds.");
                    }
                    if (!types[index].IsInstanceOfType(array))
                        throw new BridgeArgumentException($"Argument {index} is not an instance of {types[index]}.");
                }
                values[index] = array;
                continue;
            }
            if ((ValueKind)kinds[index] != ValueKind.Object)
            {
                values[index] = ValueKinds.ToObject(kinds[index], arguments[index]);
                continue;
            }
            object? argument;
            lock (Gate)
            {
                if (!entry.Handles.Objects.TryGetValue(arguments[index], out argument))
                    throw new BridgeHandleException($"Argument {index} is not an object this component holds.");
            }
            if (!types[index].IsInstanceOfType(argument))
                throw new BridgeArgumentException($"Argument {index} is not an instance of {types[index]}.");
            values[index] = argument;
        }
        return values;
    }

    // Roots a returned object under a new handle, so each return is independently owned even when it is
    // the same managed object. Null is the zero handle.
    private static ulong ReturnSlot(ulong token, Entry entry, int kind, object? returned)
    {
        if (ValueKinds.IsStruct(kind)) return StructToBlock(entry, kind, returned!);
        if (ValueKinds.IsEnum(kind)) return EnumToSlot(entry, kind, returned!);
        if ((ValueKind)kind != ValueKind.Object && !ValueKinds.IsArray(kind)) return ValueKinds.ToSlot(kind, returned);
        if (returned == null) return 0;
        lock (Gate)
        {
            // The component may have been released while the method ran; then the object is dropped.
            if (!Registrations.TryGetValue(token, out Registration? record) || !ReferenceEquals(record.Active, entry))
                throw new BridgeHandleException("The component was released during the call.");
            ulong id = checked(++lastObjectId);
            entry.Handles.Objects.Add(id, returned);
            return id;
        }
    }

    private static ulong Run(ulong token, Entry entry, MethodRecord method, object? target, ReadOnlySpan<ulong> arguments)
    {
        if (arguments.Length != method.ParameterKinds.Length)
            throw new BridgeArgumentException($"The method takes {method.ParameterKinds.Length} arguments, not {arguments.Length}.");
        object?[] values = ConvertArguments(entry, method.ParameterKinds, method.ParameterTypes, arguments);
        // No registry lock is held while user code runs. The locals root the method, target and arguments
        // for the call.
        object? returned = method.Method.Invoke(target, BindingFlags.DoNotWrapExceptions, binder: null, values, culture: null);
        return ReturnSlot(token, entry, method.ReturnKind, returned);
    }

    internal enum CreateResult { Created, NotFound, InvalidHandle }

    // Constructs an object of a looked-up type with the public constructor whose signature matches (see
    // TryFindMethod for the matching rules) and roots it in the component. A constructor exception
    // propagates unwrapped and roots nothing.
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
        RequireStructs(entry, 0, parameterKinds);
        if (type.IsAbstract || type.IsInterface) return CreateResult.NotFound;
        ConstructorInfo? constructor = null;
        foreach (ConstructorInfo candidate in type.GetConstructors(BindingFlags.Public | BindingFlags.Instance))
        {
            if (!SignatureMatches(entry, candidate.GetParameters(), parameterKinds)) continue;
            if (constructor != null)
                throw new BridgeArgumentException("The signature matches more than one constructor; object parameters match any class, so constructors that differ only by them are ambiguous.");
            constructor = candidate;
        }
        if (constructor == null) return CreateResult.NotFound;
        Type[] parameterTypes = Array.ConvertAll(constructor.GetParameters(), parameter => parameter.ParameterType);
        object?[] values = ConvertArguments(entry, parameterKinds, parameterTypes, arguments);

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

    // Finds a public field (memberKind 0) or property (memberKind 1) of a looked-up type by name and value
    // kind, static or instance, declared or inherited. By-value kinds must match exactly; Object matches any
    // class or interface type. Indexers are not properties here. Looking the same member up again returns
    // the same handle.
    internal static FindResult TryFindMember(ulong token, ulong typeId, string name, int memberKind, int valueKind, out ulong memberId)
    {
        memberId = 0;
        Entry entry;
        Type type;
        lock (Gate)
        {
            if (!Registrations.TryGetValue(token, out Registration? record) || record.Active is not Entry active ||
                !active.Handles.Types.TryGetValue(typeId, out Type? found)) return FindResult.InvalidHandle;
            entry = active;
            type = found;
        }
        if (ValueKinds.IsArray(valueKind)) throw new BridgeArgumentException("Arrays are not supported as field or property values.");
        RequireStructs(entry, valueKind, Array.Empty<int>());
        const BindingFlags flags = BindingFlags.Public | BindingFlags.Static | BindingFlags.Instance | BindingFlags.FlattenHierarchy;
        MemberRecord? member = null;
        MemberInfo? info = null;
        if (memberKind == 0)
        {
            FieldInfo? field = type.GetField(name, flags);
            if (field != null && KindMatches(entry, field.FieldType, valueKind, isReturn: false))
            {
                info = field;
                member = new MemberRecord(field, null, null, null, field.FieldType, valueKind, field.IsStatic, field.DeclaringType!);
            }
        }
        else
        {
            PropertyInfo? property = FindProperty(type, name, flags);
            if (property != null && property.GetIndexParameters().Length == 0 && KindMatches(entry, property.PropertyType, valueKind, isReturn: false))
            {
                MethodInfo? getter = property.GetGetMethod(), setter = property.GetSetMethod();
                // An init-only setter is public to reflection but only valid while the object is constructed.
                if (setter != null && Array.IndexOf(setter.ReturnParameter.GetRequiredCustomModifiers(), typeof(System.Runtime.CompilerServices.IsExternalInit)) >= 0)
                    setter = null;
                if (getter != null || setter != null)
                {
                    info = property;
                    member = new MemberRecord(null, property, getter, setter, property.PropertyType, valueKind, (getter ?? setter)!.IsStatic, property.DeclaringType!);
                }
            }
        }
        if (member == null || info == null) return FindResult.NotFound;
        lock (Gate)
        {
            if (!Registrations.TryGetValue(token, out Registration? record) || !ReferenceEquals(record.Active, entry))
                return FindResult.InvalidHandle;
            if (!entry.Handles.MemberIds.TryGetValue(info, out memberId))
            {
                memberId = checked(++lastMemberId);
                entry.Handles.MemberIds.Add(info, memberId);
                entry.Handles.Members.Add(memberId, member);
            }
            return FindResult.Found;
        }
    }

    // GetProperty throws when a derived type hides a base property with another type; the most-derived
    // declaration is the one a name means, so pick it directly.
    private static PropertyInfo? FindProperty(Type type, string name, BindingFlags flags)
    {
        PropertyInfo? best = null;
        foreach (PropertyInfo candidate in type.GetProperties(flags))
        {
            if (candidate.Name != name || candidate.GetIndexParameters().Length != 0) continue;
            if (best == null || candidate.DeclaringType!.IsSubclassOf(best.DeclaringType!)) best = candidate;
        }
        return best;
    }

    // Resolves a member handle and the object it is used on. False for an unknown component, member or
    // object handle. Throws BridgeArgumentException for static/instance misuse or an object of the wrong type.
    private static bool TryGetMember(ulong token, ulong memberId, ulong objectId, out Entry? entry, out MemberRecord? member, out object? target)
    {
        entry = null;
        member = null;
        target = null;
        lock (Gate)
        {
            if (!Registrations.TryGetValue(token, out Registration? record) || record.Active is not Entry active ||
                !active.Handles.Members.TryGetValue(memberId, out MemberRecord? found)) return false;
            if (found.IsStatic)
            {
                if (objectId != 0) throw new BridgeArgumentException("A static member cannot be used with an object.");
            }
            else
            {
                if (objectId == 0) throw new BridgeArgumentException("An instance member needs an object.");
                if (!active.Handles.Objects.TryGetValue(objectId, out target)) return false;
                if (!found.DeclaringType.IsInstanceOfType(target))
                    throw new BridgeArgumentException("The object is not an instance of the member's declaring type.");
            }
            entry = active;
            member = found;
            return true;
        }
    }

    // Reads a member. The result slot holds the value, or a new object handle for an Object member.
    internal static bool TryGetMemberValue(ulong token, ulong memberId, ulong objectId, out ulong value)
    {
        value = 0;
        if (!TryGetMember(token, memberId, objectId, out Entry? entry, out MemberRecord? member, out object? target)) return false;
        object? read;
        if (member!.Field != null)
        {
            read = member.Field.GetValue(target);
        }
        else
        {
            if (member.Getter == null) throw new BridgeArgumentException("The property is write-only.");
            read = member.Getter.Invoke(target, BindingFlags.DoNotWrapExceptions, binder: null, parameters: null, culture: null);
        }
        value = ReturnSlot(token, entry!, member.ValueKind, read);
        return true;
    }

    // Writes a member. An Object member takes an object handle or zero for null.
    internal static bool TrySetMemberValue(ulong token, ulong memberId, ulong objectId, ulong value)
    {
        if (!TryGetMember(token, memberId, objectId, out Entry? entry, out MemberRecord? member, out object? target)) return false;
        if (member!.Field != null && (member.Field.IsInitOnly || member.Field.IsLiteral))
            throw new BridgeArgumentException("The field is read-only.");
        if (member.Property != null && member.Setter == null)
            throw new BridgeArgumentException("The property is read-only.");
        object? converted;
        if ((ValueKind)member.ValueKind == ValueKind.Object)
        {
            converted = null;
            if (value != 0)
            {
                lock (Gate)
                {
                    if (!entry!.Handles.Objects.TryGetValue(value, out converted))
                        throw new BridgeHandleException("The value is not an object this component holds.");
                }
                if (!member.ValueType.IsInstanceOfType(converted))
                    throw new BridgeArgumentException($"The value is not an instance of {member.ValueType}.");
            }
        }
        else if (ValueKinds.IsStruct(member.ValueKind))
        {
            converted = StructFromSlot(entry!, member.ValueKind, value);
        }
        else if (ValueKinds.IsEnum(member.ValueKind))
        {
            converted = EnumFromSlot(entry!, member.ValueKind, value);
        }
        else
        {
            converted = ValueKinds.ToObject(member.ValueKind, value);
        }
        if (member.Field != null)
            member.Field.SetValue(target, converted);
        else
            member.Setter!.Invoke(target, BindingFlags.DoNotWrapExceptions, binder: null, new[] { converted }, culture: null);
        return true;
    }

    // Copies a struct out of the caller's bytes: a plain memory copy, then every bool byte made 0 or 1, so a
    // stray nonzero byte reads as true and managed code never sees an invalid bool.
    private static unsafe object StructFromSlot(Entry entry, int kind, ulong slot)
    {
        if (slot == 0) throw new BridgeArgumentException("A struct argument has no value.");
        StructRecord record = StructOf(entry, kind);
        object box = ManagedLayout.Box(record.Type, (byte*)slot, record.Size);
        if (record.BoolOffsets.Length > 0)
        {
            fixed (byte* data = &ManagedLayout.Data(box)) NormalizeBools(data, record.BoolOffsets);
        }
        return box;
    }

    private static unsafe void NormalizeBools(byte* origin, int[] offsets)
    {
        foreach (int offset in offsets)
            if (origin[offset] > 1) origin[offset] = 1;
    }

    // Copies a struct into a new block the caller gives back with FreeString.
    private static unsafe ulong StructToBlock(Entry entry, int kind, object value)
    {
        StructRecord record = StructOf(entry, kind);
        byte* block = (byte*)NativeMemory.Alloc((nuint)record.Size);
        ManagedLayout.Unbox(value, block, record.Size);
        return (ulong)block;
    }

    // An enum travels as its underlying integer.
    private static object EnumFromSlot(Entry entry, int kind, ulong slot)
    {
        StructRecord record = StructOf(entry, kind);
        return Enum.ToObject(record.Type, ValueKinds.ToObject(record.UnderlyingKind, slot)!);
    }

    private static ulong EnumToSlot(Entry entry, int kind, object value) =>
        ValueKinds.ToSlot(StructOf(entry, kind).UnderlyingKind, value);

    // The wire kind of a primitive, bool or char type, or -1.
    internal static int PrimitiveKindOf(Type type)
    {
        if (type == typeof(bool)) return (int)ValueKind.Bool;
        if (type == typeof(char)) return (int)ValueKind.Char;
        for (int kind = (int)ValueKind.Int8; kind <= (int)ValueKind.Double; ++kind)
            if (ValueKinds.ToType(kind, allowVoid: false) == type) return kind;
        return -1;
    }

    private static bool IsBlittableField(Type type) =>
        PrimitiveKindOf(type) >= 0 || type.IsEnum || (type.IsValueType && !type.IsPrimitive && BlittableProblem(type) == null);

    // Why a type cannot be copied byte for byte, or null if it can: a struct of sized numbers, bool, char,
    // enums, fixed-size arrays and other blittable structs, with sequential or explicit layout.
    internal static string? BlittableProblem(Type type)
    {
        if (!type.IsValueType || type.IsPrimitive || type.IsEnum || type.ContainsGenericParameters)
            return $"{type} is not a struct.";
        if (type.IsAutoLayout) return $"{type} has automatic layout, so its layout is not fixed.";
        FieldInfo[] fields = type.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
        if (fields.Length == 0) return $"{type} has no fields.";
        foreach (FieldInfo field in fields)
            if (!IsBlittableField(field.FieldType))
                return $"{type} is not blittable: field '{field.Name}' has type {field.FieldType}.";
        return null;
    }

    // How a struct field looks on the wire. Nested is the field's struct or enum type; for an inline array,
    // ElementKind, ElementNested and Length describe the element.
    private readonly record struct FieldShape(int Kind, Type? Nested, int ElementKind, Type? ElementNested, int Length);

    private static int ScalarKindOf(Type type)
    {
        int primitive = PrimitiveKindOf(type);
        if (primitive >= 0) return primitive;
        return type.IsEnum ? (int)ValueKind.Enum : (int)ValueKind.Struct;
    }

    private static Type? NestedTypeOf(Type type) => type.IsEnum || (type.IsValueType && !type.IsPrimitive) ? type : null;

    // A fixed buffer (`fixed byte Name[16]`) or an [InlineArray] struct: its element type and length.
    private static bool TryGetInlineArray(FieldInfo field, out Type element, out int length)
    {
        element = typeof(void);
        length = 0;
        if (field.GetCustomAttribute<System.Runtime.CompilerServices.FixedBufferAttribute>() is { } fixedBuffer)
        {
            element = fixedBuffer.ElementType;
            length = fixedBuffer.Length;
            return true;
        }
        if (field.FieldType.GetCustomAttribute<System.Runtime.CompilerServices.InlineArrayAttribute>() is { } inline)
        {
            FieldInfo[] inner = field.FieldType.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
            if (inner.Length != 1) return false;
            element = inner[0].FieldType;
            length = inline.Length;
            return true;
        }
        return false;
    }

    private static FieldShape ShapeOf(FieldInfo field)
    {
        if (TryGetInlineArray(field, out Type element, out int length))
            return new FieldShape((int)ValueKind.InlineArray, null, ScalarKindOf(element), NestedTypeOf(element), length);
        int kind = ScalarKindOf(field.FieldType);
        return new FieldShape(kind, kind is (int)ValueKind.Struct or (int)ValueKind.Enum ? field.FieldType : null, 0, null, 0);
    }

    private static string KindName(int kind) => kind switch
    {
        (int)ValueKind.Struct => "a struct",
        (int)ValueKind.Enum => "an enum",
        (int)ValueKind.InlineArray => "a fixed-size array",
        _ => ValueKinds.ToType(kind, allowVoid: false).Name
    };

    // Registers a struct or enum the native side maps, after checking the native description against the
    // managed type. The description is text. A struct is a first line "TypeName|size", then one line per
    // field in declaration order, "name|offset|kind|nestedTypeName" (nestedTypeName is empty unless kind is
    // Struct or Enum, and a nested type must be registered first); an inline array field has six parts,
    // "name|offset|InlineArray|elementNestedTypeName|elementKind|length". An enum is one line,
    // "TypeName|size|underlyingKind". Any mismatch throws BridgeArgumentException, naming it. Registering the
    // same type again validates again and returns the same handle.
    internal static FindResult TryRegisterStruct(ulong token, string description, out ulong structId)
    {
        structId = 0;
        Entry entry;
        lock (Gate)
        {
            if (!Registrations.TryGetValue(token, out Registration? record) || record.Active is not Entry active)
                return FindResult.InvalidHandle;
            entry = active;
        }
        string[] lines = description.Split('\n');
        string[] header = lines[0].Split('|');
        bool isEnum = header.Length == 3;
        if ((header.Length != 2 && !isEnum) || (isEnum && lines.Length != 1) ||
            !int.TryParse(header[1], System.Globalization.NumberStyles.None, System.Globalization.CultureInfo.InvariantCulture, out int nativeSize))
            throw new BridgeArgumentException("The struct description is malformed.");
        Type? type = entry.Assembly.GetType(header[0], throwOnError: false, ignoreCase: false);
        if (type == null || type.Assembly != entry.Assembly || !type.IsVisible) return FindResult.NotFound;

        StructRecord created;
        if (isEnum)
        {
            if (!int.TryParse(header[2], System.Globalization.NumberStyles.None, System.Globalization.CultureInfo.InvariantCulture, out int nativeUnderlying))
                throw new BridgeArgumentException("The enum description is malformed.");
            if (!type.IsEnum) throw new BridgeArgumentException($"{type} is not an enum.");
            Type underlyingType = Enum.GetUnderlyingType(type);
            int underlying = PrimitiveKindOf(underlyingType);
            if (underlying != nativeUnderlying)
                throw new BridgeArgumentException($"{type} is an enum of {underlyingType.Name}, which does not match the native enum's underlying type.");
            int enumSize = ManagedLayout.SizeOf(type);
            if (enumSize != nativeSize)
                throw new BridgeArgumentException($"{type} is {enumSize} bytes but the native enum is {nativeSize}.");
            created = new StructRecord(type, enumSize, Array.Empty<int>(), underlying);
        }
        else
        {
            created = ValidateStruct(entry, type, nativeSize, lines);
        }
        lock (Gate)
        {
            if (!Registrations.TryGetValue(token, out Registration? record) || !ReferenceEquals(record.Active, entry))
                return FindResult.InvalidHandle;
            if (!entry.Handles.StructIds.TryGetValue(type, out structId))
            {
                structId = checked(++lastStructId);
                entry.Handles.StructIds.Add(type, structId);
                entry.Handles.Structs.Add(structId, created);
            }
            return FindResult.Found;
        }
    }

    private static StructRecord ValidateStruct(Entry entry, Type type, int nativeSize, string[] lines)
    {
        if (BlittableProblem(type) is string problem) throw new BridgeArgumentException(problem);

        // Reflection does not promise declaration order; the metadata token does.
        FieldInfo[] fields = type.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
        Array.Sort(fields, (left, right) => left.MetadataToken.CompareTo(right.MetadataToken));
        int managedSize = ManagedLayout.SizeOf(type);
        if (managedSize != nativeSize)
            throw new BridgeArgumentException($"{type} is {managedSize} bytes but the native struct is {nativeSize}.");
        if (lines.Length - 1 != fields.Length)
            throw new BridgeArgumentException($"{type} has {fields.Length} fields but the native struct declares {lines.Length - 1}.");
        var bools = new List<int>();
        for (int index = 0; index < fields.Length; ++index)
        {
            string[] parts = lines[index + 1].Split('|');
            if ((parts.Length != 4 && parts.Length != 6) || !int.TryParse(parts[1], System.Globalization.NumberStyles.None, System.Globalization.CultureInfo.InvariantCulture, out int offset) ||
                !int.TryParse(parts[2], System.Globalization.NumberStyles.None, System.Globalization.CultureInfo.InvariantCulture, out int kind))
                throw new BridgeArgumentException("The struct description is malformed.");
            FieldInfo field = fields[index];
            if (field.Name != parts[0])
                throw new BridgeArgumentException($"Field {index} of {type} is '{field.Name}' but the native struct has '{parts[0]}' there (field order must match).");
            int managedOffset = ManagedLayout.OffsetOf(field);
            if (managedOffset != offset)
                throw new BridgeArgumentException($"Field '{field.Name}' of {type} is at offset {managedOffset} but the native field is at {offset}.");
            FieldShape shape = ShapeOf(field);
            if ((ValueKind)kind == ValueKind.InlineArray)
            {
                if (parts.Length != 6 || !int.TryParse(parts[4], System.Globalization.NumberStyles.None, System.Globalization.CultureInfo.InvariantCulture, out int elementKind) ||
                    !int.TryParse(parts[5], System.Globalization.NumberStyles.None, System.Globalization.CultureInfo.InvariantCulture, out int length))
                    throw new BridgeArgumentException("The struct description is malformed.");
                if (shape.Kind != kind)
                    throw new BridgeArgumentException($"Field '{field.Name}' of {type} is {field.FieldType}, which does not match the native field's type (a fixed-size array).");
                if (shape.ElementKind != elementKind || !SameNested(entry, shape.ElementNested, elementKind, parts[3]))
                    throw new BridgeArgumentException($"The elements of field '{field.Name}' of {type} are {KindName(shape.ElementKind)}, which does not match the native array's element type.");
                if (shape.Length != length)
                    throw new BridgeArgumentException($"Field '{field.Name}' of {type} holds {shape.Length} elements but the native array holds {length}.");
                AddBoolOffsets(entry, bools, offset, shape.ElementKind, shape.ElementNested, length);
                continue;
            }
            if (parts.Length != 4 || shape.Kind != kind || !SameNested(entry, shape.Nested, kind, parts[3]))
            {
                string detail = shape.Nested != null && shape.Kind == kind
                    ? "which the native struct does not map to the same type"
                    : "which does not match the native field's type";
                throw new BridgeArgumentException($"Field '{field.Name}' of {type} is {field.FieldType}, {detail}.");
            }
            AddBoolOffsets(entry, bools, offset, kind, shape.Nested, 1);
        }
        return new StructRecord(type, managedSize, bools.ToArray());
    }

    // For a struct or enum kind: the type is already registered and has the native side's name. Any other
    // kind has no nested type.
    private static bool SameNested(Entry entry, Type? nested, int kind, string nativeName)
    {
        if (kind != (int)ValueKind.Struct && kind != (int)ValueKind.Enum) return nativeName.Length == 0;
        lock (Gate)
            return nested != null && entry.Handles.StructIds.ContainsKey(nested) && nested.FullName == nativeName;
    }

    // Adds the bool byte offsets of `count` consecutive elements starting at `offset`.
    private static void AddBoolOffsets(Entry entry, List<int> bools, int offset, int kind, Type? nested, int count)
    {
        if (kind == (int)ValueKind.Bool)
        {
            for (int index = 0; index < count; ++index) bools.Add(offset + index);
        }
        else if (kind == (int)ValueKind.Struct)
        {
            StructRecord inner;
            lock (Gate) inner = entry.Handles.Structs[entry.Handles.StructIds[nested!]];
            if (inner.BoolOffsets.Length == 0) return;
            for (int index = 0; index < count; ++index)
                foreach (int innerOffset in inner.BoolOffsets) bools.Add(offset + index * inner.Size + innerOffset);
        }
    }

    // Creates an array (kind: see ValueKind.Array) and roots it like an object, so it blocks release until
    // freed. False for an unknown component.
    internal static bool TryCreateArray(ulong token, int kind, long length, out ulong objectId)
    {
        objectId = 0;
        Entry entry;
        lock (Gate)
        {
            if (!Registrations.TryGetValue(token, out Registration? record) || record.Active is not Entry active) return false;
            entry = active;
        }
        if (!ValueKinds.IsArray(kind)) throw new BridgeArgumentException($"Unsupported array kind {kind}.");
        if (length < 0) throw new BridgeArgumentException("An array length cannot be negative.");
        Array array = Array.CreateInstance(ElementTypeOf(entry, kind), length);
        lock (Gate)
        {
            if (!Registrations.TryGetValue(token, out Registration? record) || !ReferenceEquals(record.Active, entry)) return false;
            objectId = checked(++lastObjectId);
            entry.Handles.Objects.Add(objectId, array);
            return true;
        }
    }

    // Resolves a rooted one-dimensional array. A kind of zero skips the element-type check. False for an
    // unknown component or object handle; BridgeArgumentException if the object is not an array of that element type.
    private static bool TryGetArray(ulong token, ulong objectId, int kind, out Entry? entry, out Array? array)
    {
        entry = null;
        array = null;
        object? found;
        lock (Gate)
        {
            if (!Registrations.TryGetValue(token, out Registration? record) || record.Active is not Entry active ||
                !active.Handles.Objects.TryGetValue(objectId, out found)) return false;
            entry = active;
        }
        if (found is not Array candidate || !candidate.GetType().IsSZArray)
            throw new BridgeArgumentException("The object is not a one-dimensional array.");
        if (kind != 0 && candidate.GetType().GetElementType() != ElementTypeOf(entry, kind))
            throw new BridgeArgumentException($"The array's element type is {candidate.GetType().GetElementType()}, not the requested type.");
        array = candidate;
        return true;
    }

    internal static bool TryGetArrayLength(ulong token, ulong objectId, out long length)
    {
        length = 0;
        if (!TryGetArray(token, objectId, 0, out _, out Array? array)) return false;
        length = array!.LongLength;
        return true;
    }

    // Element access names the array type it expects, so a bad kind can never read as a quiet zero.
    private static void RequireArrayKind(int kind)
    {
        if (!ValueKinds.IsArray(kind)) throw new BridgeArgumentException($"Unsupported array kind {kind}.");
    }

    private static void CheckIndex(Array array, long index)
    {
        if (index < 0 || index >= array.LongLength)
            throw new BridgeArgumentException($"Index {index} is outside an array of length {array.LongLength}.");
    }

    // Reads one element: a primitive's bits, or a new string or struct block for the caller to give back.
    internal static bool TryGetArrayElement(ulong token, ulong objectId, int kind, long index, out ulong slot)
    {
        slot = 0;
        RequireArrayKind(kind);
        if (!TryGetArray(token, objectId, kind, out Entry? entry, out Array? array)) return false;
        CheckIndex(array!, index);
        object? value = array!.GetValue(index);
        int elementKind = ValueKinds.ElementKind(kind);
        slot = ValueKinds.IsStruct(elementKind) ? StructToBlock(entry!, elementKind, value!) : ValueKinds.ToSlot(elementKind, value);
        return true;
    }

    internal static bool TrySetArrayElement(ulong token, ulong objectId, int kind, long index, ulong slot)
    {
        RequireArrayKind(kind);
        if (!TryGetArray(token, objectId, kind, out Entry? entry, out Array? array)) return false;
        CheckIndex(array!, index);
        int elementKind = ValueKinds.ElementKind(kind);
        object? value = ValueKinds.IsStruct(elementKind) ? StructFromSlot(entry!, elementKind, slot) : ValueKinds.ToObject(elementKind, slot);
        array!.SetValue(value, index);
        return true;
    }

    private static int PrimitiveSize(int kind) => (ValueKind)kind switch
    {
        ValueKind.Bool or ValueKind.Int8 or ValueKind.UInt8 => 1,
        ValueKind.Int16 or ValueKind.UInt16 or ValueKind.Char => 2,
        ValueKind.Int32 or ValueKind.UInt32 or ValueKind.Float => 4,
        ValueKind.Int64 or ValueKind.UInt64 or ValueKind.Double => 8,
        _ => throw new BridgeArgumentException("Bulk copies need primitive or struct elements.")
    };

    // Copies count elements between the array (from start) and a caller buffer of the same layout. Only
    // primitive and struct elements can be copied.
    internal static unsafe bool TryCopyArray(ulong token, ulong objectId, int kind, long start, long count, void* buffer, bool intoArray)
    {
        RequireArrayKind(kind);
        if (!TryGetArray(token, objectId, kind, out Entry? entry, out Array? array)) return false;
        int elementKind = ValueKinds.ElementKind(kind);
        StructRecord? structElement = ValueKinds.IsStruct(elementKind) ? StructOf(entry!, elementKind) : null;
        int size = structElement?.Size ?? PrimitiveSize(elementKind);
        if (start < 0 || count < 0 || (ulong)start + (ulong)count > (ulong)array!.LongLength)
            throw new BridgeArgumentException($"The range {start}..{(ulong)start + (ulong)count} is outside an array of length {array!.LongLength}.");
        if (count == 0) return true;
        if (buffer == null) throw new BridgeArgumentException("A bulk copy needs a buffer.");
        ulong bytes = checked((ulong)count * (ulong)size);
        fixed (byte* data = &System.Runtime.InteropServices.MemoryMarshal.GetArrayDataReference(array))
        {
            byte* element = data + (ulong)start * (ulong)size;
            if (intoArray)
            {
                Buffer.MemoryCopy(buffer, element, bytes, bytes);
                // Managed code is only given 0 or 1 for a bool, whatever the native bytes were.
                if ((ValueKind)elementKind == ValueKind.Bool)
                    for (long index = 0; index < count; ++index) if (element[index] > 1) element[index] = 1;
                if (structElement != null && structElement.BoolOffsets.Length > 0)
                    for (long index = 0; index < count; ++index) NormalizeBools(element + index * size, structElement.BoolOffsets);
            }
            else
            {
                Buffer.MemoryCopy(element, buffer, bytes, bytes);
            }
        }
        return true;
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

    // Waits for a released component to be collected: while it is Pending, runs a full blocking collection
    // (with finalizers) and looks again, until it is Collected or timeoutMs has passed, in which case the
    // state is still Pending. A component that is still loaded or already collected returns at once, without
    // collecting. False for an unknown token.
    internal static bool TryWaitForUnload(ulong token, int timeoutMs, out ComponentUnloadState state)
    {
        var clock = System.Diagnostics.Stopwatch.StartNew();
        if (!TryGetUnloadState(token, out state)) return false;
        while (state == ComponentUnloadState.Pending)
        {
            GC.Collect();
            GC.WaitForPendingFinalizers();
            GC.Collect();
            if (!TryGetUnloadState(token, out state)) return false;
            long remaining = timeoutMs - clock.ElapsedMilliseconds;
            if (state != ComponentUnloadState.Pending || remaining <= 0) break;
            System.Threading.Thread.Sleep((int)Math.Min(remaining, 10));
        }
        return true;
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
