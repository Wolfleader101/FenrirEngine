using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Fenrir.ManagedHost;

public static class Bootstrap
{
    // The native host resolves this method through its assembly-loading runtime delegate.
    // This is not a native DLL export and must not be called directly from C#.
    // The native function pointer must use the matching int32_t(), Cdecl signature.
    // Keep this handshake free of exceptions; unmanaged callers cannot catch managed exceptions.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetBridgeVersion() => 1;
}
