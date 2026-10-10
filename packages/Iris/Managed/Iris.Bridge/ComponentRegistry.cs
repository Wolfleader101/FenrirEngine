using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;

namespace Iris.Bridge;

internal static class ComponentRegistry
{
    private sealed record Entry(ComponentLoadContext Context, Assembly Assembly, string Name);
    private sealed class Registration(Entry entry)
    {
        internal Entry? Active = entry;
        internal readonly WeakReference Context = new(entry.Context, trackResurrection: true);
    }
    private static readonly object Gate = new();
    private static readonly Dictionary<ulong, Registration> Registrations = new();
    private static ulong lastToken;

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
