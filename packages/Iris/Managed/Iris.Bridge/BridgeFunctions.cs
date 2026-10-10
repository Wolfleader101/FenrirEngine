using System.Runtime.InteropServices;

namespace Iris.Bridge;

// The bridge ABI: one table of Cdecl function pointers, filled by Bootstrap.GetBridgeFunctions.
// Field order, count and signatures must match Detail::BridgeFunctions in the native host.
// Any change to the table bumps Version. Assigning &Method to a field type-checks its signature.
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct BridgeFunctions
{
    internal const int Version = 15;

    internal delegate* unmanaged[Cdecl]<byte*, int, ulong*, byte*, int, int*, int*, int> LoadComponent;
    internal delegate* unmanaged[Cdecl]<ulong, byte*, int, int*, byte*, int, int*, int*, int> CopyComponentNameUtf8;
    internal delegate* unmanaged[Cdecl]<ulong, byte*, int, int*, int*, int> ReleaseComponent;
    internal delegate* unmanaged[Cdecl]<ulong, int*, byte*, int, int*, int*, int> GetComponentUnloadState;
    internal delegate* unmanaged[Cdecl]<ulong, byte*, int, int*, int*, int> ForgetComponentUnload;
    internal delegate* unmanaged[Cdecl]<byte*, int, byte*, int, int*, int*, int> RegisterSharedAssembly;
    internal delegate* unmanaged[Cdecl]<ulong, byte*, int, ulong*, byte*, int, int*, int*, int> FindType;
    internal delegate* unmanaged[Cdecl]<ulong, ulong, int, byte*, int, int*, byte*, int, int*, int*, int> CopyTypeNameUtf8;
    internal delegate* unmanaged[Cdecl]<ulong, ulong, byte*, int, int, int*, int, ulong*, byte*, int, int*, int*, int> FindMethod;
    internal delegate* unmanaged[Cdecl]<ulong, ulong, ulong*, int, ulong*, byte*, int, int*, int*, int> InvokeMethod;
    internal delegate* unmanaged[Cdecl]<void*, void*, byte*, int, int*, int*, int> SetLogSink;
    internal delegate* unmanaged[Cdecl]<ulong, byte*, int, int*, int*, int> ForceReleaseComponent;
    internal delegate* unmanaged[Cdecl]<ulong, ulong, byte*, int, int, int*, int, ulong*, byte*, int, int*, int*, int> FindInstanceMethod;
    internal delegate* unmanaged[Cdecl]<ulong, ulong, ulong, ulong*, int, ulong*, byte*, int, int*, int*, int> InvokeInstanceMethod;
    internal delegate* unmanaged[Cdecl]<ulong, ulong, int*, ulong*, int, ulong*, byte*, int, int*, int*, int> CreateObject;
    internal delegate* unmanaged[Cdecl]<ulong, ulong, byte*, int, int*, int*, int> ReleaseObject;

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
