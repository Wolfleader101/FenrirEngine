using System;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;

namespace Iris.Bridge;

internal sealed class ComponentLoadContext(AssemblyDependencyResolver resolver) : AssemblyLoadContext(isCollectible: true)
{
    protected override Assembly? Load(AssemblyName name)
    {
        // Shared assemblies (the bridge and declared contracts) always resolve to their single copy,
        // even if the component deploys a private one.
        Assembly? shared = SharedAssemblies.Resolve(name);
        if (shared != null) return shared;
        string? path = resolver.ResolveAssemblyToPath(name);
        return path == null ? null : LoadFromAssemblyPath(path);
    }

    protected override IntPtr LoadUnmanagedDll(string name)
    {
        string? path = resolver.ResolveUnmanagedDllToPath(name);
        return path == null ? IntPtr.Zero : LoadUnmanagedDllFromPath(path);
    }
}
