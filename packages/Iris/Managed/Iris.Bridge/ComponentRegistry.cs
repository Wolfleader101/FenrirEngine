using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;

namespace Iris.Bridge;

internal static class ComponentRegistry
{
    // Handles to types looked up in one component. They live in the active entry, so releasing the
    // component drops every Type reference with it; no handle outlives its component.
    private sealed class TypeTable
    {
        internal readonly Dictionary<Type, ulong> ByType = new();
        internal readonly Dictionary<ulong, Type> ById = new();
    }
    private sealed record Entry(ComponentLoadContext Context, Assembly Assembly, string Name)
    {
        internal TypeTable Types { get; } = new();
    }
    private sealed class Registration(Entry entry)
    {
        internal Entry? Active = entry;
        internal readonly WeakReference Context = new(entry.Context, trackResurrection: true);
    }
    private static readonly object Gate = new();
    private static readonly Dictionary<ulong, Registration> Registrations = new();
    private static ulong lastToken;
    private static ulong lastTypeId; // Shared by all components, so a type handle is never reused.

    internal enum FindResult { Found, NotFound, InvalidHandle }

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
            var record = new Registration(new Entry(context, assembly, assembly.GetName().Name ?? throw new InvalidOperationException("Assembly has no name.")));
            lock (Gate)
            {
                ulong token = checked(++lastToken); // Never reuse IDs, even after a failed registration.
                Registrations.Add(token, record);
                registered = true;
                return token;
            }
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
            if (!entry.Types.ByType.TryGetValue(type, out typeId))
            {
                typeId = checked(++lastTypeId);
                entry.Types.ByType.Add(type, typeId);
                entry.Types.ById.Add(typeId, type);
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
                !entry.Types.ById.TryGetValue(typeId, out Type? type)) return false;
            text = field == 0 ? type.FullName ?? type.Name : type.Assembly.GetName().Name ?? "";
            return true;
        }
    }

    internal static bool TryInvokeInt32(ulong token, string typeName, string methodName, int left, int right, out int result)
    {
        result = 0;
        Assembly assembly;
        lock (Gate)
        {
            if (!Registrations.TryGetValue(token, out Registration? record) || record.Active is not Entry entry) return false;
            assembly = entry.Assembly;
        }
        // No registry lock is held while resolving types or executing user code.
        // This local assembly/delegate roots an accepted in-flight call until it returns.
        result = ComponentInvocation.InvokeInt32(assembly, typeName, methodName, left, right);
        return true;
    }

    internal static bool Release(ulong token)
    {
        Entry? entry;
        lock (Gate)
        {
            if (!Registrations.TryGetValue(token, out Registration? record) || record.Active == null) return false;
            entry = record.Active;
            // Invalidate access and drop all strong fields before running unload handlers.
            record.Active = null;
        }
        // No permanent registry root remains, even if an unload handler throws.
        entry.Context.Unload();
        return true;
    }

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
