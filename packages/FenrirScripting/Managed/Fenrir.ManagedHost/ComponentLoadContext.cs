using System;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;

namespace Fenrir.ManagedHost;

internal sealed class ComponentLoadContext(AssemblyDependencyResolver resolver) : AssemblyLoadContext(isCollectible: true)
{
    protected override Assembly? Load(AssemblyName name)
    {
        // Share the resident bridge if requested; never load a collectible copy of it.
        Assembly bridge = typeof(Bootstrap).Assembly;
        AssemblyName resident = bridge.GetName();
        if (string.Equals(name.Name, resident.Name, StringComparison.OrdinalIgnoreCase))
        {
            // Returning an existing assembly from Load is not enough to enforce
            // reference compatibility. Explicitly validate before sharing it.
            if ((name.Version != null && name.Version > resident.Version) ||
                !string.Equals(name.CultureName ?? string.Empty, resident.CultureName ?? string.Empty, StringComparison.OrdinalIgnoreCase) ||
                !(name.GetPublicKeyToken() ?? Array.Empty<byte>()).AsSpan().SequenceEqual(resident.GetPublicKeyToken() ?? Array.Empty<byte>()))
                throw new FileLoadException($"Resident bridge '{resident.FullName}' cannot satisfy reference '{name.FullName}'.");
            return bridge;
        }
        string? path = resolver.ResolveAssemblyToPath(name);
        return path == null ? null : LoadFromAssemblyPath(path);
    }

    protected override IntPtr LoadUnmanagedDll(string name)
    {
        string? path = resolver.ResolveUnmanagedDllToPath(name);
        return path == null ? IntPtr.Zero : LoadUnmanagedDllFromPath(path);
    }
}
