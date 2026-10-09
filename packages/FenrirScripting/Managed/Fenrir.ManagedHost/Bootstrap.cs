using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Fenrir.ManagedHost;

public static class Bootstrap
{
    // The native host will obtain this method through a runtime delegate.
    // This is not a native DLL export and must not be called directly from C#.
    // The eventual native function pointer must use the matching Cdecl convention.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetBridgeVersion() => 1;
}
