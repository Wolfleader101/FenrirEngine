using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using Iris.Bridge;

// The host-wide log sink through the real SetLogSink export. Runs last-ish: the sink is process-wide.
static unsafe class LogChecks
{
    private const int Debug = 0, Info = 1, Warning = 2, Error = 3;
    private static readonly List<(int Level, string Text)> Messages = new();

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void Capture(void* context, int level, byte* text, int length)
    {
        Messages.Add((level, new UTF8Encoding(false, true).GetString(text, length)));
    }

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    private static int SetSink(delegate* unmanaged[Cdecl]<void*, int, byte*, int, void> callback, void* context, byte* diagnostic = null, int capacity = 0)
    {
        delegate* unmanaged[Cdecl]<delegate* unmanaged[Cdecl]<void*, int, byte*, int, void>, void*, byte*, int, int*, int*, int> set = &Bootstrap.SetLogSink;
        int written = 99, truncated = 99;
        int status = set(callback, context, diagnostic, capacity, &written, &truncated);
        Check(written == 0 && truncated == 0, "SetLogSink wrote diagnostic metadata");
        return status;
    }

    internal static void Run(string componentPath)
    {
        string path = System.IO.Path.GetFullPath(componentPath);
        Messages.Clear();
        delegate* unmanaged[Cdecl]<void*, int, byte*, int, void> capture = &Capture;
        Check(SetSink(capture, null) == 0, "Registering a sink failed");
        ulong token = 0;
        try
        {
            // Bridge events reach the sink at Info.
            token = ComponentRegistry.Load(path);
            Check(Messages.Exists(message => message.Level == Info && message.Text.Contains("Iris.Examples.Component", StringComparison.Ordinal)),
                  "Loading a component was not logged at Info");

            // A component exception reaches the sink at Error, and the call still returns its own details.
            Messages.Clear();
            InvocationChecks.Result thrown = InvocationChecks.Invoke(token, InvocationChecks.TypeName, "Throw");
            Check(thrown.Status == 3 && thrown.Diagnostic.Contains("InvalidOperationException"), "The call lost its own diagnostic");
            Check(Messages.Exists(message => message.Level == Error && message.Text.Contains("InvalidOperationException", StringComparison.Ordinal) &&
                                              message.Text.Contains("π \U0001F43A", StringComparison.Ordinal)),
                  "A component exception was not reported at Error with its details");

            // The bridge's own argument validation is a Warning, and a plain NotFound is not an exception.
            Messages.Clear();
            delegate* unmanaged[Cdecl]<ulong, byte*, int, ulong*, byte*, int, int*, int*, int> findType = &Bootstrap.FindType;
            byte[] bad = { 0xFF };
            ulong typeId = 0;
            int written = 0, truncated = 0;
            fixed (byte* name = bad)
                Check(findType(token, name, bad.Length, &typeId, null, 0, &written, &truncated) == 1, "Malformed UTF-8 was accepted");
            Check(Messages.Exists(message => message.Level == Warning && message.Text.Contains("DecoderFallbackException", StringComparison.Ordinal)),
                  "A validation failure was not reported at Warning");
            Messages.Clear();
            Check(InvocationChecks.Invoke(token, "Iris.Examples.Component.Missing", "Add").Status == 6, "Expected a NotFound");
            Check(!Messages.Exists(message => message.Level == Error), "A NotFound was reported as an error");

            // Clearing the sink stops delivery; calls keep working.
            Check(SetSink(null, null) == 0, "Clearing the sink failed");
            Messages.Clear();
            Check(InvocationChecks.Invoke(token, InvocationChecks.TypeName, "Throw").Status == 3, "A call failed without a sink");
            Check(Messages.Count == 0, "Messages were delivered after the sink was cleared");

            // Pointer validation: the diagnostic outputs are mandatory.
            delegate* unmanaged[Cdecl]<delegate* unmanaged[Cdecl]<void*, int, byte*, int, void>, void*, byte*, int, int*, int*, int> set = &Bootstrap.SetLogSink;
            Check(set(null, null, null, 0, null, &truncated) == 1 && set(null, null, null, 0, &written, null) == 1 &&
                  set(null, null, null, 1, &written, &truncated) == 1, "Invalid SetLogSink storage was accepted");
        }
        finally
        {
            SetSink(null, null);
            if (token != 0) Check(ComponentRegistry.Release(token) && ComponentRegistry.ForgetUnload(token), "Release failed");
        }
        Console.WriteLine("Log sink: Info events, Error exception reports with details, Warning validation failures, no NotFound noise, clearing and validation passed.");
    }
}
