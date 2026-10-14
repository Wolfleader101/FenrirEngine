using System;
using System.IO;
using System.Text;
using Iris.Bridge;

// Invocation through the real FindType / FindMethod / InvokeMethod exports: diagnostic bounds and
// canaries, lookup and call statuses, pointer validation, and recovery. MethodChecks covers value kinds.
static class InvocationChecks
{
    internal const string TypeName = "Iris.Examples.Component.Calculations";
    internal readonly record struct Result(int Status, int Value, string Diagnostic, int Truncated);

    private const int Int32 = (int)ValueKind.Int32;

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    // Looks up and calls `static int method(int, int)`. The status is the first step that failed, so a
    // missing type or method is NotFound (6). Only the final call's diagnostic storage is canary-checked.
    internal static unsafe Result Invoke(ulong token, string type, string method, int left = 20, int right = 22, int capacity = 4096)
    {
        delegate* unmanaged[Cdecl]<ulong, byte*, int, ulong*, byte*, int, int*, int*, int> findType = &Bootstrap.FindType;
        delegate* unmanaged[Cdecl]<ulong, ulong, byte*, int, int, int*, int, ulong*, byte*, int, int*, int*, int> findMethod = &Bootstrap.FindMethod;
        delegate* unmanaged[Cdecl]<ulong, ulong, ulong*, int, ulong*, byte*, int, int*, int*, int> invoke = &Bootstrap.InvokeMethod;
        byte[] typeBytes = Encoding.UTF8.GetBytes(type), methodBytes = Encoding.UTF8.GetBytes(method);
        byte[] storage = new byte[capacity + 2];
        Array.Fill(storage, (byte)0xA5);
        int[] kinds = { Int32, Int32 };
        ulong[] arguments = { unchecked((ulong)(long)left), unchecked((ulong)(long)right) };
        fixed (byte* typePointer = typeBytes, methodPointer = methodBytes, diagnostic = storage)
        fixed (int* kindPointer = kinds)
        fixed (ulong* argumentPointer = arguments)
        {
            ulong typeId = 0, methodId = 0, slot = 0;
            int written = 99, truncated = 99;
            byte* buffer = capacity == 0 ? null : diagnostic + 1;
            int status = findType(token, typePointer, typeBytes.Length, &typeId, buffer, capacity, &written, &truncated);
            if (status == 0)
                status = findMethod(token, typeId, methodPointer, methodBytes.Length, Int32, kindPointer, kinds.Length, &methodId,
                                    buffer, capacity, &written, &truncated);
            if (status == 0)
            {
                slot = 99; // The export must clear this on failure.
                status = invoke(token, methodId, argumentPointer, arguments.Length, &slot, buffer, capacity, &written, &truncated);
            }
            Check(storage[0] == 0xA5 && storage[^1] == 0xA5 && written >= 0 && written <= capacity &&
                  (truncated == 0 || truncated == 1), "Invocation corrupted diagnostic storage/metadata");
            Check(status == 0 || slot == 0, "Failed invocation published a value");
            string text = new UTF8Encoding(false, true).GetString(storage.AsSpan(1, written));
            return new Result(status, unchecked((int)(long)slot), text, truncated);
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

            // Lookup failures are NotFound with the name; call failures are ManagedException with details.
            foreach ((string lookupType, string lookupMethod) in new[] {
                (TypeName, "Missing"), (TypeName, "WrongReturn"), (TypeName, "Generic"), (TypeName, "Hidden"),
                (TypeName, "UnmanagedOnly"), (TypeName, "add"), ("Iris.Examples.Component.Missing", "Add")
            })
            {
                Result failed = Invoke(token, lookupType, lookupMethod);
                Check(failed.Status == 6 && failed.Diagnostic.Length > 0, "Lookup failure was not a NotFound with a diagnostic: " + lookupType + "." + lookupMethod);
                Check(Invoke(token, TypeName, "Add").Value == 42, "Valid invocation did not recover after a failure");
            }
            Result thrown = Invoke(token, TypeName, "Throw");
            Check(thrown.Status == 3 && thrown.Diagnostic.Contains("InvalidOperationException", StringComparison.Ordinal),
                  "A component exception was not contained with its details");
            Result overflow = Invoke(token, TypeName, "Add", int.MaxValue, 1);
            Check(overflow.Status == 3 && overflow.Diagnostic.Contains("OverflowException", StringComparison.Ordinal),
                  "An OverflowException was not a ManagedException");
            foreach (int capacity in new[] { 0, 1, 33, 4096 })
            {
                Result bounded = Invoke(token, TypeName, "LongError", capacity: capacity);
                Check(bounded.Status == 3 && bounded.Truncated == 1, "Bounded component diagnostic changed operation status");
            }
            Check(Invoke(token, TypeName, "Add").Diagnostic == "", "Successful call retained diagnostics");

            // Pointer and capacity validation on the new exports.
            delegate* unmanaged[Cdecl]<ulong, byte*, int, ulong*, byte*, int, int*, int*, int> findType = &Bootstrap.FindType;
            delegate* unmanaged[Cdecl]<ulong, ulong, byte*, int, int, int*, int, ulong*, byte*, int, int*, int*, int> findMethod = &Bootstrap.FindMethod;
            delegate* unmanaged[Cdecl]<ulong, ulong, ulong*, int, ulong*, byte*, int, int*, int*, int> invoke = &Bootstrap.InvokeMethod;
            byte[] name = Encoding.UTF8.GetBytes(TypeName), method = Encoding.UTF8.GetBytes("Add"), storage = new byte[256];
            int[] kinds = { Int32, Int32 };
            ulong[] arguments = { 20, 22 };
            fixed (byte* typePointer = name, methodPointer = method, diagnostic = storage)
            fixed (int* kindPointer = kinds)
            fixed (ulong* argumentPointer = arguments)
            {
                ulong typeId = 0, methodId = 0, slot = 99;
                int written = 99, truncated = 99;
                Check(findType(token, typePointer, name.Length, &typeId, diagnostic, storage.Length, &written, &truncated) == 0, "Type lookup failed");
                Check(findMethod(token, typeId, methodPointer, method.Length, Int32, kindPointer, 2, &methodId, diagnostic, storage.Length, &written, &truncated) == 0,
                      "Method lookup failed");
                Check(findType(token, typePointer, name.Length, null, diagnostic, storage.Length, &written, &truncated) == 1 && written == 0 && truncated == 0,
                      "Null type output accepted");
                Check(findMethod(token, typeId, methodPointer, method.Length, Int32, kindPointer, 2, null, diagnostic, storage.Length, &written, &truncated) == 1,
                      "Null method output accepted");
                Check(findMethod(token, typeId, methodPointer, method.Length, Int32, null, 2, &methodId, diagnostic, storage.Length, &written, &truncated) == 1 && methodId == 0,
                      "Null parameter kinds with a count were accepted");
                Check(findMethod(token, typeId, methodPointer, method.Length, Int32, kindPointer, -1, &methodId, diagnostic, storage.Length, &written, &truncated) == 1,
                      "A negative parameter count was accepted");
                Check(findMethod(token, typeId, null, method.Length, Int32, kindPointer, 2, &methodId, diagnostic, storage.Length, &written, &truncated) == 1 &&
                      findMethod(token, typeId, methodPointer, -1, Int32, kindPointer, 2, &methodId, diagnostic, storage.Length, &written, &truncated) == 1,
                      "A null or negative-length method name was accepted");
                Check(findMethod(token, typeId, methodPointer, method.Length, Int32, kindPointer, 2, &methodId, diagnostic, storage.Length, null, &truncated) == 1 &&
                      findMethod(token, typeId, methodPointer, method.Length, Int32, kindPointer, 2, &methodId, diagnostic, storage.Length, &written, null) == 1,
                      "Null diagnostic metadata was accepted");
                Check(findMethod(token, typeId, methodPointer, method.Length, Int32, kindPointer, 2, &methodId, null, 1, &written, &truncated) == 1 &&
                      findMethod(token, typeId, methodPointer, method.Length, Int32, kindPointer, 2, &methodId, diagnostic, -1, &written, &truncated) == 1,
                      "Invalid diagnostic storage was accepted");
                // Failed lookups clear their output, so resolve the handle again for the call checks.
                Check(findMethod(token, typeId, methodPointer, method.Length, Int32, kindPointer, 2, &methodId, diagnostic, storage.Length, &written, &truncated) == 0 && methodId != 0,
                      "Method lookup failed after validation failures");
                Check(invoke(token, methodId, argumentPointer, 2, null, diagnostic, storage.Length, &written, &truncated) == 1, "Null result accepted");
                Check(invoke(token, methodId, null, 2, &slot, diagnostic, storage.Length, &written, &truncated) == 1 && slot == 0,
                      "Null arguments with a count were accepted");
                Check(invoke(token, methodId, argumentPointer, -1, &slot, diagnostic, storage.Length, &written, &truncated) == 1, "A negative argument count was accepted");
                Check(invoke(token, methodId, argumentPointer, 2, &slot, diagnostic, storage.Length, null, &truncated) == 1 &&
                      invoke(token, methodId, argumentPointer, 2, &slot, diagnostic, storage.Length, &written, null) == 1,
                      "Null diagnostic metadata was accepted by InvokeMethod");
                Check(invoke(token, methodId, argumentPointer, 2, &slot, null, 1, &written, &truncated) == 1, "Invalid diagnostic storage accepted by InvokeMethod");
                Check(invoke(token, methodId, argumentPointer, 2, &slot, diagnostic, storage.Length, &written, &truncated) == 0 && slot == 42,
                      "A valid call failed after validation failures");
            }
            Check(Invoke(token, "", "Add").Status == 1 && Invoke(token, TypeName, "").Status == 1, "Empty names accepted");
            Check(Invoke(0, TypeName, "Add").Status == 5, "Zero token accepted");
        }
        finally { Check(ComponentRegistry.Release(token), "Invocation registration release failed"); }
        Check(Invoke(token, TypeName, "Add").Status == 5 && ComponentRegistry.Count == baseline, "Released token remained usable or registration leaked");
        Check(ComponentRegistry.ForgetUnload(token), "Invocation unload observation cleanup failed");
        Console.WriteLine("Managed invocation: lookup statuses, contained exceptions, bounded diagnostics, pointer validation, recovery and stale tokens passed.");
    }
}
