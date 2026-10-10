using System;
using System.IO;
using System.Text;
using Fenrir.ManagedHost;

static class InvocationChecks
{
    internal const string TypeName = "Fenrir.HostingComponent.Calculations";
    internal readonly record struct Result(int Status, int Value, string Diagnostic, int Truncated);

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    internal static unsafe Result Invoke(ulong token, string type, string method, int left = 20, int right = 22, int capacity = 4096)
    {
        byte[] typeBytes = Encoding.UTF8.GetBytes(type), methodBytes = Encoding.UTF8.GetBytes(method);
        byte[] storage = new byte[capacity + 2];
        Array.Fill(storage, (byte)0xA5);
        delegate* unmanaged[Cdecl]<ulong, byte*, int, byte*, int, int, int, int*, byte*, int, int*, int*, int> invoke = &Bootstrap.InvokeComponentInt32;
        fixed (byte* typePointer = typeBytes, methodPointer = methodBytes, diagnostic = storage)
        {
            int value = 99, written = 99, truncated = 99;
            int status = invoke(token, typePointer, typeBytes.Length, methodPointer, methodBytes.Length,
                                left, right, &value, diagnostic + 1, capacity, &written, &truncated);
            Check(storage[0] == 0xA5 && storage[^1] == 0xA5 && written >= 0 && written <= capacity &&
                  (truncated == 0 || truncated == 1), "Invocation corrupted diagnostic storage/metadata");
            Check(status == 0 || value == 0, "Failed invocation published a value");
            string text = new UTF8Encoding(false, true).GetString(storage.AsSpan(1, written));
            return new Result(status, value, text, truncated);
        }
    }

    // A fresh load, invocation and release through the exports proves the resident bridge still works.
    internal static void CheckResidentBridge(string componentPath)
    {
        ulong token = ComponentRegistry.Load(Path.GetFullPath(componentPath));
        Check(Invoke(token, TypeName, "Add").Value == 42, "Resident bridge stopped working");
        Check(ComponentRegistry.Release(token) && ComponentRegistry.ForgetUnload(token), "Resident bridge cleanup failed");
    }

    internal static unsafe void Run(string componentPath)
    {
        int baseline = ComponentRegistry.Count;
        ulong token = ComponentRegistry.Load(Path.GetFullPath(componentPath));
        try
        {
            Result good = Invoke(token, TypeName, "Add");
            Check(good.Status == 0 && good.Value == 42 && good.Diagnostic == "" && good.Truncated == 0,
                  "Component/dependency calculation or exact overload selection failed");
            foreach ((string type, string method, string exception) in new[] {
                (TypeName, "Missing", "MissingMethodException"),
                (TypeName, "WrongReturn", "MissingMethodException"),
                (TypeName, "WrongParameters", "MissingMethodException"),
                (TypeName, "Generic", "MissingMethodException"),
                (TypeName, "ByRef", "MissingMethodException"),
                (TypeName, "Hidden", "MissingMethodException"),
                (TypeName, "UnmanagedOnly", "MissingMethodException"),
                (TypeName, "add", "MissingMethodException"),
                ("Fenrir.HostingComponent.InstanceCalculation", "Add", "MissingMethodException"),
                ("Fenrir.HostingComponent.OpenCalculation`1", "Add", "MissingMethodException"),
                ("Fenrir.HostingComponent.HiddenCalculation", "Add", "MissingMethodException"),
                ("Fenrir.HostingComponent.Missing", "Add", "TypeLoadException"),
                (TypeName, "Throw", "InvalidOperationException"),
                (TypeName, "ThrowArgument", "ArgumentException")
            })
            {
                Result failed = Invoke(token, type, method);
                Check(failed.Status == 3 && failed.Diagnostic.Contains(exception, StringComparison.Ordinal),
                      "Lookup/component exception was not contained with useful diagnostics: " + type + "." + method);
                Check(Invoke(token, TypeName, "Add").Value == 42, "Valid invocation did not recover after a failure");
            }
            Result overflow = Invoke(token, TypeName, "Add", int.MaxValue, 1);
            Check(overflow.Status == 2 && overflow.Diagnostic.Contains("OverflowException", StringComparison.Ordinal), "Overflow status/details lost");
            foreach (int capacity in new[] { 0, 1, 33, 4096 })
            {
                Result bounded = Invoke(token, TypeName, "LongError", capacity: capacity);
                Check(bounded.Status == 3 && bounded.Truncated == 1, "Bounded component diagnostic changed operation status");
            }
            Check(Invoke(token, TypeName, "Add").Diagnostic == "", "Successful call retained diagnostics");

            byte[] typeBytes = Encoding.UTF8.GetBytes(TypeName), methodBytes = Encoding.UTF8.GetBytes("Add"), storage = new byte[256];
            delegate* unmanaged[Cdecl]<ulong, byte*, int, byte*, int, int, int, int*, byte*, int, int*, int*, int> invoke = &Bootstrap.InvokeComponentInt32;
            fixed (byte* type = typeBytes, method = methodBytes, diagnostic = storage)
            {
                int value = 99, written = 99, truncated = 99;
                Check(invoke(token, type, typeBytes.Length, method, methodBytes.Length, 20, 22, null, diagnostic, storage.Length, &written, &truncated) == 1 &&
                      written == 0 && truncated == 0, "Null value output accepted");
                Check(invoke(token, type, typeBytes.Length, method, methodBytes.Length, 20, 22, &value, diagnostic, storage.Length, null, &truncated) == 1 &&
                      value == 0 && truncated == 0, "Null diagnostic count accepted");
                Check(invoke(token, type, typeBytes.Length, method, methodBytes.Length, 20, 22, &value, diagnostic, storage.Length, &written, null) == 1 &&
                      value == 0 && written == 0, "Null truncation output accepted");
                Check(invoke(token, type, typeBytes.Length, method, methodBytes.Length, 20, 22, &value, null, 1, &written, &truncated) == 1 && value == 0,
                      "Null diagnostic with capacity accepted");
                Check(invoke(token, type, typeBytes.Length, method, methodBytes.Length, 20, 22, &value, diagnostic, -1, &written, &truncated) == 1 && value == 0,
                      "Negative diagnostic capacity accepted");
                Check(invoke(token, null, typeBytes.Length, method, methodBytes.Length, 20, 22, &value, diagnostic, storage.Length, &written, &truncated) == 1 && value == 0,
                      "Null type name accepted");
                Check(invoke(token, type, -1, method, methodBytes.Length, 20, 22, &value, diagnostic, storage.Length, &written, &truncated) == 1 && value == 0,
                      "Negative type length accepted");
                Check(invoke(token, type, typeBytes.Length, null, methodBytes.Length, 20, 22, &value, diagnostic, storage.Length, &written, &truncated) == 1 && value == 0,
                      "Null method name accepted");
                Check(invoke(token, type, typeBytes.Length, method, -1, 20, 22, &value, diagnostic, storage.Length, &written, &truncated) == 1 && value == 0,
                      "Negative method length accepted");
                typeBytes[0] = 0xFF;
                Check(invoke(token, type, typeBytes.Length, method, methodBytes.Length, 20, 22, &value, diagnostic, storage.Length, &written, &truncated) == 1 && value == 0 &&
                      new UTF8Encoding(false, true).GetString(storage.AsSpan(0, written)).Contains("DecoderFallbackException"),
                      "Malformed type UTF-8 was accepted or omitted diagnostics");
            }
            Check(Invoke(token, "", "Add").Status == 1 && Invoke(token, TypeName, "").Status == 1 &&
                  Invoke(token, TypeName + "\0suffix", "Add").Status == 1 && Invoke(token, TypeName, "Add\0suffix").Status == 1,
                  "Empty/embedded-null names accepted");
            Check(Invoke(0, TypeName, "Add").Status == 5, "Zero token accepted");
        }
        finally { Check(ComponentRegistry.Release(token), "Invocation registration release failed"); }
        Check(Invoke(token, TypeName, "Add").Status == 5 && ComponentRegistry.Count == baseline, "Released token remained usable or registration leaked");
        Check(ComponentRegistry.ForgetUnload(token), "Invocation unload observation cleanup failed");
        Console.WriteLine("Managed invocation: exact signatures, pointer/UTF-8 validation, exceptions, diagnostics, recovery and stale tokens passed.");
    }
}
