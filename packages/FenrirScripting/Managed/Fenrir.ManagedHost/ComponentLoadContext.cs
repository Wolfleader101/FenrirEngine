using System;
using System.Reflection;
using System.Runtime.Loader;

namespace Fenrir.ManagedHost;

internal sealed class ComponentLoadContext(AssemblyDependencyResolver resolver) : AssemblyLoadContext(isCollectible: true)
{
    protected override Assembly? Load(AssemblyName name)
    {
        // Share the resident bridge if requested; never load a collectible copy of it.
        Assembly bridge = typeof(Bootstrap).Assembly;
        if (string.Equals(name.Name, bridge.GetName().Name, StringComparison.OrdinalIgnoreCase)) return bridge;
        string? path = resolver.ResolveAssemblyToPath(name);
        return path == null ? null : LoadFromAssemblyPath(path);
    }

    protected override IntPtr LoadUnmanagedDll(string name)
    {
        string? path = resolver.ResolveUnmanagedDllToPath(name);
        return path == null ? IntPtr.Zero : LoadUnmanagedDllFromPath(path);
    }
}
