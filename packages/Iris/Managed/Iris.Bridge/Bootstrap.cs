using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace Iris.Bridge;

public static partial class Bootstrap
{
    // The only public entry point. The native host resolves it by name through the runtime's
    // get_function_pointer delegate, then obtains every other export from the filled table.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int GetBridgeFunctions(int version, void* table, int size,
        int* reportedVersion, int* reportedSize)
        => BridgeFunctions.Handshake(version, (BridgeFunctions*)table, size, reportedVersion, reportedSize,
            BridgeFunctions.Version, sizeof(BridgeFunctions), new BridgeFunctions
            {
                LoadComponent = &LoadComponent,
                CopyComponentNameUtf8 = &CopyComponentNameUtf8,
                ReleaseComponent = &ReleaseComponent,
                GetComponentUnloadState = &GetComponentUnloadState,
                ForgetComponentUnload = &ForgetComponentUnload,
                RegisterSharedAssembly = &RegisterSharedAssembly,
                FindType = &FindType,
                CopyTypeNameUtf8 = &CopyTypeNameUtf8,
                FindMethod = &FindMethod,
                InvokeMethod = &InvokeMethod,
                SetLogSink = &SetLogSink,
                ForceReleaseComponent = &ForceReleaseComponent,
                FindInstanceMethod = &FindInstanceMethod,
                InvokeInstanceMethod = &InvokeInstanceMethod,
                CreateObject = &CreateObject,
                ReleaseObject = &ReleaseObject
            });

    // A null destination with zero capacity queries the size; requiredBytes is mandatory.
    private static unsafe bool ValidateUtf8Output(byte* destination, int capacity, int* requiredBytes)
    {
        if (requiredBytes == null)
        {
            return false;
        }
        *requiredBytes = 0;
        return capacity >= 0 && (destination != null || capacity == 0);
    }

    // Sizes count UTF-8 bytes, excluding any BOM or terminator. Insufficient capacity writes no
    // payload. Called only after validation, inside an export's try block.
    private static unsafe BridgeStatus CopyUtf8Payload(string text, Encoding encoding, byte* destination,
                                                      int capacity, int* requiredBytes)
    {
        int byteCount = encoding.GetByteCount(text);
        *requiredBytes = byteCount;
        if (destination == null)
        {
            return BridgeStatus.Success;
        }
        if (capacity < byteCount)
        {
            return BridgeStatus.BufferTooSmall;
        }
        // The span borrows caller-owned memory and covers only the payload.
        encoding.GetBytes(text.AsSpan(), new Span<byte>(destination, byteCount));
        return BridgeStatus.Success;
    }
}
