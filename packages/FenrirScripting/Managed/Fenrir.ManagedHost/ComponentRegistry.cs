using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;

namespace Fenrir.ManagedHost;

internal static class ComponentRegistry
{
    private sealed record Entry(ComponentLoadContext Context, Assembly Assembly, string Name);
    private static readonly object Gate = new();
    private static readonly Dictionary<ulong, Entry> Entries = new();
    private static ulong lastToken;

    // Internal verification seam; never a native/public diagnostic API.
    internal static int Count { get { lock (Gate) return Entries.Count; } }

    internal static ulong Load(string path)
    {
        if (!Path.IsPathFullyQualified(path) || path.Contains('\0'))
            throw new ArgumentException("A component path must be absolute and contain no null characters.");
        if (string.Equals(AssemblyName.GetAssemblyName(path).Name, typeof(Bootstrap).Assembly.GetName().Name, StringComparison.OrdinalIgnoreCase))
            throw new ArgumentException("The resident bridge cannot be loaded as a collectible component.");

        // Resolver creation can fail. Do it before creating a collectible context.
        var resolver = new AssemblyDependencyResolver(path);
        var context = new ComponentLoadContext(resolver);
        bool registered = false;
        try
        {
            Assembly assembly = context.LoadFromAssemblyPath(path);
            if (!context.IsCollectible || !assembly.IsCollectible || AssemblyLoadContext.GetLoadContext(assembly) != context)
                throw new InvalidOperationException("Component loading did not produce a collectible assembly in its own context.");
            var entry = new Entry(context, assembly, assembly.GetName().Name ?? throw new InvalidOperationException("Assembly has no name."));
            lock (Gate)
            {
                ulong token = checked(++lastToken); // Never reuse IDs, even after a failed registration.
                Entries.Add(token, entry);
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
        lock (Gate) return Entries.TryGetValue(token, out Entry? entry) ? entry.Name : null;
    }

    internal static bool Release(ulong token)
    {
        Entry? entry;
        lock (Gate)
        {
            if (!Entries.Remove(token, out entry)) return false;
        }
        // No permanent registry root remains, even if an unload handler throws.
        entry.Context.Unload();
        return true;
    }
}
