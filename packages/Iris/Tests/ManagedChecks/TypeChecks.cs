using System;
using System.Text;
using Iris.Bridge;

// Type handles through the real FindType / CopyTypeNameUtf8 exports.
static unsafe class TypeChecks
{
    private const string Calculations = "Iris.Examples.Component.Calculations";
    private const string Assembly = "Iris.Examples.Component";
    private const int FullName = 0, AssemblyName = 1;

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    private static int Find(ulong token, byte[] name, out ulong id, out string diagnostic)
    {
        delegate* unmanaged[Cdecl]<ulong, byte*, int, ulong*, byte*, int, int*, int*, int> find = &Bootstrap.FindType;
        byte[] storage = new byte[4096];
        fixed (byte* namePointer = name, diagnosticPointer = storage)
        {
            ulong value = 99;
            int written = 99, truncated = 99;
            int status = find(token, namePointer, name.Length, &value, diagnosticPointer, storage.Length, &written, &truncated);
            Check(status == 0 || value == 0, "A failed lookup published a handle");
            id = value;
            diagnostic = new UTF8Encoding(false, true).GetString(storage, 0, written);
            return status;
        }
    }

    private static int Find(ulong token, string name, out ulong id, out string diagnostic) =>
        Find(token, Encoding.UTF8.GetBytes(name), out id, out diagnostic);

    private static int CopyName(ulong token, ulong typeId, int field, out string text)
    {
        delegate* unmanaged[Cdecl]<ulong, ulong, int, byte*, int, int*, byte*, int, int*, int*, int> copy = &Bootstrap.CopyTypeNameUtf8;
        int required = 99, written = 99, truncated = 99;
        int status = copy(token, typeId, field, null, 0, &required, null, 0, &written, &truncated);
        text = "";
        if (status != 0) return status;
        byte[] payload = new byte[required];
        fixed (byte* pointer = payload)
        {
            int copied = 0;
            status = copy(token, typeId, field, pointer, payload.Length, &copied, null, 0, &written, &truncated);
            Check(status == 0 && copied == required, "Type name copy disagreed with its size query");
        }
        text = new UTF8Encoding(false, true).GetString(payload);
        return status;
    }

    private static void Release(ulong token)
    {
        Check(ComponentRegistry.Release(token) && ComponentRegistry.ForgetUnload(token), "Type check release failed");
    }

    internal static void Run(string componentPath)
    {
        string path = System.IO.Path.GetFullPath(componentPath);
        ulong token = ComponentRegistry.Load(path);
        ulong staleId;
        try
        {
            Check(Find(token, Calculations, out ulong id, out string diagnostic) == 0 && id != 0 && diagnostic == "",
                  "A public type was not found");
            Check(CopyName(token, id, FullName, out string fullName) == 0 && fullName == Calculations &&
                  CopyName(token, id, AssemblyName, out string assemblyName) == 0 && assemblyName == Assembly,
                  "Type name or assembly name incorrect");
            Check(Find(token, Calculations, out ulong again, out _) == 0 && again == id,
                  "Repeating a lookup created another handle");
            Check(Find(token, "Iris.Examples.Component.InstanceCalculation", out ulong other, out _) == 0 && other != 0 && other != id,
                  "Two types shared a handle");

            // Not found: a missing name, a non-public type, an open generic, a type from a dependency.
            foreach (string name in new[]
            {
                "Iris.Examples.Component.Missing",
                "Iris.Examples.Component.HiddenCalculation",
                "Iris.Examples.Component.OpenCalculation`1",
                "Iris.Examples.Dependency.DependencyMath"
            })
            {
                Check(Find(token, name, out ulong missing, out string notFound) == 6 && missing == 0 &&
                      notFound.Contains(name, StringComparison.Ordinal), "Lookup of '" + name + "' was not a NotFound with its name");
            }
            Check(Find(token, Calculations, out ulong recovered, out _) == 0 && recovered == id, "Lookup did not recover after a NotFound");

            // Argument validation.
            Check(Find(token, Array.Empty<byte>(), out _, out _) == 1, "An empty name was accepted");
            Check(Find(token, Encoding.UTF8.GetBytes("A\0B"), out _, out _) == 1, "A name with a null was accepted");
            Check(Find(token, new byte[] { 0xFF }, out _, out string decoder) == 1 && decoder.Contains("DecoderFallbackException"),
                  "Malformed UTF-8 was accepted or undiagnosed");

            // Handle validation: unknown, zero, bad field, and another component's handle.
            Check(CopyName(token, id + 1000, FullName, out _) == 5 && CopyName(token, 0, FullName, out _) == 5, "An unknown type handle was accepted");
            Check(CopyName(token, id, 7, out _) == 1, "An unknown name field was accepted");
            Check(CopyName(0, id, FullName, out _) == 5, "Component token zero was accepted");
            ulong second = ComponentRegistry.Load(path);
            try
            {
                Check(CopyName(second, id, FullName, out _) == 5, "A handle worked on another component");
                Check(Find(second, Calculations, out ulong secondId, out _) == 0 && secondId > other && secondId != id,
                      "Handles were reused across components");
            }
            finally { Release(second); }
            staleId = id;
        }
        finally { Release(token); }

        // After release, the old component's handles and new lookups are invalid, and nothing is reused.
        Check(Find(token, Calculations, out _, out _) == 5, "A released component still resolved types");
        Check(CopyName(token, staleId, FullName, out _) == 5 && !ComponentRegistry.TryGetTypeName(token, staleId, 0, out _),
              "A released component's type handle remained valid");
        ulong reloaded = ComponentRegistry.Load(path);
        try
        {
            Check(Find(reloaded, Calculations, out ulong freshId, out _) == 0 && freshId > staleId, "A handle was reused after reload");
            Check(CopyName(reloaded, staleId, FullName, out _) == 5, "A stale handle selected a type in a new component");
        }
        finally { Release(reloaded); }
        Console.WriteLine("Type handles: lookup, names, NotFound, argument and handle validation, and release invalidation passed.");
    }
}
