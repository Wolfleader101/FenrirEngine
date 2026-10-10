using System;
using Iris.Bridge;

static class HandshakeChecks
{
    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    internal static unsafe void Run()
    {
        // Native code declares the same six pointer-sized slots; a different count changes the size.
        Check(sizeof(BridgeFunctions) == 6 * IntPtr.Size, "Bridge table is not six pointer-sized slots");

        delegate* unmanaged[Cdecl]<int, void*, int, int*, int*, int> handshake = &Bootstrap.GetBridgeFunctions;
        int size = sizeof(BridgeFunctions);
        byte[] storage = new byte[size];
        fixed (byte* table = storage)
        {
            int version = 99, reported = 99;
            Array.Fill(storage, (byte)0xA5);
            Check(handshake(BridgeFunctions.Version + 1, table, size, &version, &reported) == 1 &&
                  version == BridgeFunctions.Version && reported == size &&
                  Array.TrueForAll(storage, item => item == 0xA5), "Version mismatch wrote the table");
            Check(handshake(BridgeFunctions.Version, table, size - 1, &version, &reported) == 1 &&
                  Array.TrueForAll(storage, item => item == 0xA5), "Size mismatch wrote the table");
            Check(handshake(BridgeFunctions.Version, table, size, null, &reported) == 1 &&
                  handshake(BridgeFunctions.Version, null, size, &version, &reported) == 1,
                  "Missing handshake storage accepted");
            Check(handshake(BridgeFunctions.Version, table, size, &version, &reported) == 0, "Matching handshake failed");
            BridgeFunctions* filled = (BridgeFunctions*)table;
            Check(filled->LoadComponent != null && filled->ForgetComponentUnload != null, "Table entries missing");
            int state = 99, written = 99, truncated = 99;
            Check(filled->GetComponentUnloadState(0, &state, null, 0, &written, &truncated) == 5 && state == -1,
                  "Table entry did not reach the export");
        }
        Console.WriteLine("Managed handshake: version/size mismatch leaves the table untouched; a match fills callable entries.");
    }
}
