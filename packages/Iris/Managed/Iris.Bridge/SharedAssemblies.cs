using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;

namespace Iris.Bridge;

// Assemblies that every component shares as a single copy in the default load context: the bridge
// itself, plus contract assemblies the host declares (for example a scripting API that components
// compile against). A component's reference to a shared assembly always resolves to that copy,
// even when the component deploys a private one, so types keep one identity across components.
// Registration is process-wide and permanent, like the default load context it fills.
internal static class SharedAssemblies
{
    private static readonly object Gate = new();
    private static readonly Dictionary<string, Assembly> ByName = new(StringComparer.OrdinalIgnoreCase)
    {
        [typeof(Bootstrap).Assembly.GetName().Name!] = typeof(Bootstrap).Assembly
    };

    // Loads the assembly into the default context and shares it. Registering the same file again is
    // a no-op; a different file with an already shared name is rejected.
    internal static void Register(string path)
    {
        if (!Path.IsPathFullyQualified(path) || path.Contains('\0'))
            throw new BridgeArgumentException("A shared assembly path must be absolute and contain no null characters.");
        string name = AssemblyName.GetAssemblyName(path).Name
            ?? throw new BridgeArgumentException("The shared assembly has no name.");
        lock (Gate)
        {
            if (ByName.TryGetValue(name, out Assembly? existing))
            {
                StringComparison paths = OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;
                if (string.Equals(Path.GetFullPath(existing.Location), Path.GetFullPath(path), paths))
                    return;
                throw new BridgeArgumentException($"An assembly named '{name}' is already shared from '{existing.Location}'.");
            }
            ByName.Add(name, AssemblyLoadContext.Default.LoadFromAssemblyPath(path));
        }
    }

    internal static bool IsShared(string? name)
    {
        if (name == null) return false;
        lock (Gate) return ByName.ContainsKey(name);
    }

    // Returns the shared copy for a reference, or null when the name is not shared. A reference the
    // shared copy cannot satisfy (a newer version, another culture or public key) is rejected rather
    // than silently bound to the wrong assembly.
    internal static Assembly? Resolve(AssemblyName reference)
    {
        Assembly? shared;
        lock (Gate)
        {
            if (reference.Name == null || !ByName.TryGetValue(reference.Name, out shared)) return null;
        }
        AssemblyName resident = shared.GetName();
        if ((reference.Version != null && reference.Version > resident.Version) ||
            !string.Equals(reference.CultureName ?? string.Empty, resident.CultureName ?? string.Empty, StringComparison.OrdinalIgnoreCase) ||
            !(reference.GetPublicKeyToken() ?? Array.Empty<byte>()).AsSpan().SequenceEqual(resident.GetPublicKeyToken() ?? Array.Empty<byte>()))
            throw new FileLoadException($"Shared assembly '{resident.FullName}' cannot satisfy reference '{reference.FullName}'.");
        return shared;
    }
}
