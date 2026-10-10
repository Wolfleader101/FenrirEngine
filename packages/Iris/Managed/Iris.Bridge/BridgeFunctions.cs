using System.Runtime.InteropServices;

namespace Iris.Bridge;

// The bridge ABI: one table of Cdecl function pointers, filled by Bootstrap.GetBridgeFunctions.
// Field order, count and signatures must match Detail::BridgeFunctions in the native host.
// Any change to the table bumps Version. Assigning &Method to a field type-checks its signature.
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct BridgeFunctions
{
    internal const int Version = 10;

    internal delegate* unmanaged[Cdecl]<byte*, int, ulong*, byte*, int, int*, int*, int> LoadComponent;
    internal delegate* unmanaged[Cdecl]<ulong, byte*, int, int*, byte*, int, int*, int*, int> CopyComponentNameUtf8;
    internal delegate* unmanaged[Cdecl]<ulong, byte*, int, int*, int*, int> ReleaseComponent;
    internal delegate* unmanaged[Cdecl]<ulong, byte*, int, byte*, int, int, int, int*, byte*, int, int*, int*, int> InvokeComponentInt32;
    internal delegate* unmanaged[Cdecl]<ulong, int*, byte*, int, int*, int*, int> GetComponentUnloadState;
    internal delegate* unmanaged[Cdecl]<ulong, byte*, int, int*, int*, int> ForgetComponentUnload;
    internal delegate* unmanaged[Cdecl]<byte*, int, byte*, int, int*, int*, int> RegisterSharedAssembly;

    // Writes the reported version and table size, then fills the table only if both match the
    // caller's. Never throws: native code cannot catch a managed exception from this entry point.
    internal static int Handshake(int version, BridgeFunctions* table, int size, int* reportedVersion,
        int* reportedSize, int answerVersion, int answerSize, in BridgeFunctions functions)
    {
        if (reportedVersion == null || reportedSize == null) return (int)BridgeStatus.InvalidArgument;
        *reportedVersion = answerVersion;
        *reportedSize = answerSize;
        if (table == null || version != answerVersion || size != answerSize) return (int)BridgeStatus.InvalidArgument;
        *table = functions;
        return (int)BridgeStatus.Success;
    }
}
