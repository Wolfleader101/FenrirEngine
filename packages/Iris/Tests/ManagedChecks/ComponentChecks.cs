using System;
using System.IO;
using System.Text;
using Iris.Bridge;

static class ComponentChecks
{
    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    internal static unsafe void Run(string componentPath)
    {
        delegate* unmanaged[Cdecl]<byte*, int, ulong*, byte*, int, int*, int*, int> load = &Bootstrap.LoadComponent;
        delegate* unmanaged[Cdecl]<ulong, byte*, int, int*, byte*, int, int*, int*, int> name = &Bootstrap.CopyComponentNameUtf8;
        delegate* unmanaged[Cdecl]<ulong, byte*, int, int*, int*, int> release = &Bootstrap.ReleaseComponent;
        byte[] path = Encoding.UTF8.GetBytes(Path.GetFullPath(componentPath));
        byte[] diagnostic = new byte[4096], output = new byte[128];
        var strictUtf8 = new UTF8Encoding(false, true);
        int baseline = ComponentRegistry.Count;
        int trackingBaseline = ComponentRegistry.TrackingCount;
        string invalidFile = Path.GetTempFileName();
        File.WriteAllText(invalidFile, "This is not an assembly.");
        try
        {
            fixed (byte* pathPointer = path, error = diagnostic, payload = output)
            {
                ulong token = 99;
                int written = 99, truncated = 99, required = 99;
                Check(load(pathPointer, path.Length, &token, error, diagnostic.Length, &written, &truncated) == 0 &&
                      token != 0 && written == 0 && truncated == 0 && ComponentRegistry.Count == baseline + 1,
                      "Load did not publish exactly one collectible registration");
                ulong oldToken = token;
                Check(name(token, null, 0, &required, error, diagnostic.Length, &written, &truncated) == 0 && required > 0,
                      "Identity query failed");
                Array.Fill(output, (byte)0xA5);
                Check(name(token, payload + 1, 1, &required, error, diagnostic.Length, &written, &truncated) == 4 &&
                      Array.TrueForAll(output, item => item == 0xA5), "Small identity buffer wrote payload");
                Check(name(token, payload + 1, output.Length - 2, &required, error, diagnostic.Length, &written, &truncated) == 0 &&
                      strictUtf8.GetString(output.AsSpan(1, required)) == "Iris.Examples.Component" &&
                      output[0] == 0xA5 && output[required + 1] == 0xA5, "Identity copy corrupted storage");
                Check(release(token, null, 1, &written, &truncated) == 1 && ComponentRegistry.Count == baseline + 1,
                      "Invalid release arguments changed registration");
                Check(release(token, null, 0, &written, &truncated) == 0 && ComponentRegistry.Count == baseline,
                      "Release retained registration roots");
                Check(release(token, error, diagnostic.Length, &written, &truncated) == 5 &&
                      name(token, null, 0, &required, error, diagnostic.Length, &written, &truncated) == 5 && required == 0,
                      "Released token remained usable");
                Check(name(0, null, 0, &required, error, diagnostic.Length, &written, &truncated) == 5,
                      "Invalid token zero accepted");
                Check(ComponentRegistry.ForgetUnload(oldToken), "First unload observation cleanup failed");
                Check(load(pathPointer, path.Length, &token, error, diagnostic.Length, &written, &truncated) == 0 &&
                      token > oldToken, "Token was reused");
                Check(release(token, error, diagnostic.Length, &written, &truncated) == 0, "Second registration release failed");
                Check(ComponentRegistry.ForgetUnload(token), "Second unload observation cleanup failed");

                Check(load(pathPointer, path.Length, null, error, diagnostic.Length, &written, &truncated) == 1 &&
                      written == 0 && truncated == 0, "Null token output accepted");
                Check(load(null, 0, &token, error, diagnostic.Length, &written, &truncated) == 1 && token == 0,
                      "Empty path accepted");
                Check(load(pathPointer, path.Length, &token, null, 1, &written, &truncated) == 1 && token == 0,
                      "Invalid diagnostic pointer accepted");
            }

            foreach ((byte[] bytes, int expectedStatus, string exceptionName) in new[] {
                (Encoding.UTF8.GetBytes(invalidFile), 3, "BadImageFormatException"),
                (Encoding.UTF8.GetBytes(invalidFile + ".missing"), 3, "FileNotFoundException"),
                (new byte[] { 0xFF }, 1, "DecoderFallbackException"),
                (Encoding.UTF8.GetBytes("relative.dll"), 1, "ArgumentException"),
                (Encoding.UTF8.GetBytes(Path.GetFullPath(componentPath) + "\0suffix"), 1, "ArgumentException"),
                (Encoding.UTF8.GetBytes(typeof(Bootstrap).Assembly.Location), 1, "ArgumentException")
            })
            {
                fixed (byte* pathPointer = bytes, error = diagnostic)
                {
                    ulong token = 99;
                    int written = 99, truncated = 99;
                    Check(load(pathPointer, bytes.Length, &token, error, diagnostic.Length, &written, &truncated) == expectedStatus &&
                          token == 0 && ComponentRegistry.Count == baseline &&
                          strictUtf8.GetString(diagnostic.AsSpan(0, written)).Contains(exceptionName),
                          "Failed load leaked a registration, published output, or omitted diagnostics");
                }
            }
            // A valid framework image reaches context loading, but cannot become
            // an owned collectible copy of CoreLib. Verify rollback after context creation.
            byte[] coreLibrary = Encoding.UTF8.GetBytes(typeof(object).Assembly.Location);
            fixed (byte* pathPointer = coreLibrary, error = diagnostic)
            {
                ulong token = 99;
                int written = 99, truncated = 99;
                Check(load(pathPointer, coreLibrary.Length, &token, error, diagnostic.Length, &written, &truncated) == 3 &&
                      token == 0 && written > 0 && ComponentRegistry.Count == baseline,
                      "Failure after context creation did not roll back registration");
            }
        }
        finally { File.Delete(invalidFile); }
        Check(ComponentRegistry.Count == baseline && ComponentRegistry.TrackingCount == trackingBaseline, "Component checks leaked registrations/observations");
        Console.WriteLine("Managed components: collectible loading, identity, token validation, release and failure cleanup passed.");
    }
}
