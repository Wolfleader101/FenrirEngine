using System;
using System.Text;
using Iris.Bridge;

// In-process checks of the bridge's managed code and exports. Each group runs and reports
// separately; a failing group does not stop the others. "--unload" runs the unload checks alone,
// in their own process, so nothing else roots the contexts they collect.
static class Checks
{
    private sealed class TextException(string text) : Exception
    {
        public override string ToString() => text;
    }

    private sealed class BrokenException : Exception
    {
        public override string ToString() => throw new InvalidOperationException("Diagnostic formatting failed");
    }

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    static int Main(string[] args)
    {
        InternalCallChecks.RegisterAll();
        if (args.Length == 2 && args[0] == "--unload")
            return RunGroups(("unloading", () => UnloadChecks.Run(args[1])));
        if (args.Length != 1)
        {
            Console.Error.WriteLine("Usage: ManagedChecks [--unload] component.dll");
            return 1;
        }
        string component = args[0];
        return RunGroups(
            ("handshake", HandshakeChecks.Run),
            ("diagnostic text", DiagnosticTextChecks),
            ("export contracts", ExportContractChecks.Run),
            ("value codec", ValueCodecChecks.Run),
            ("struct mapping", StructMappingChecks.Run),
            ("numbering", NumberingChecks.Run),
            ("components", () => ComponentChecks.Run(component)),
            ("invocation", () => InvocationChecks.Run(component)),
            ("dependencies", () => DependencyChecks.Run(component)),
            ("type handles", () => TypeChecks.Run(component)),
            ("method handles", () => MethodChecks.Run(component)),
            ("objects", () => ObjectChecks.Run(component)),
            ("object passing", () => ObjectChecks.RunPassing(component)),
            ("members", () => MemberChecks.Run(component)),
            ("strings", () => StringChecks.Run(component)),
            ("structs", () => StructChecks.Run(component)),
            ("arrays", () => ArrayChecks.Run(component)),
            ("internal calls", () => InternalCallChecks.Run(component)),
            ("type queries", () => TypeQueryChecks.Run(component)),
            ("log sink", () => LogChecks.Run(component)),
            ("resident bridge after the other groups", () => InvocationChecks.CheckResidentBridge(component)),
            // Last: sharing is process-wide and permanent.
            ("shared assemblies", () => SharedAssemblyChecks.Run(component)));
    }

    private static int RunGroups(params (string Name, Action Run)[] groups)
    {
        int failures = 0;
        foreach ((string name, Action run) in groups)
        {
            try
            {
                run();
                Console.WriteLine($"PASS {name}");
            }
            catch (Exception exception)
            {
                ++failures;
                Console.WriteLine($"FAIL {name}: {exception}");
            }
        }
        Console.WriteLine(failures == 0 ? "All groups passed." : $"{failures} group(s) failed.");
        return failures == 0 ? 0 : 1;
    }

    // ManagedDiagnostic.Write at every capacity boundary, and with a throwing formatter.
    private static void DiagnosticTextChecks()
    {
        var strictUtf8 = new UTF8Encoding(false, true);
        // Every possible boundary, including capacities too small for the first scalar.
        foreach (string text in new[] { "", "ASCII", "π\U0001F43A€!", "A\0B", "\uD800X" })
        {
            byte[] expected = Encoding.UTF8.GetBytes(text);
            for (int capacity = 0; capacity <= expected.Length + 1; ++capacity)
            {
                byte[] storage = new byte[capacity + 2];
                Array.Fill(storage, (byte)0xA5);
                ManagedDiagnostic.Write(new TextException(text), storage.AsSpan(1, capacity), out int written, out int truncated);
                Check(written >= 0 && written <= capacity, "Invalid reported count");
                Check(storage[0] == 0xA5 && storage[capacity + 1] == 0xA5, "Buffer canary overwritten");
                ReadOnlySpan<byte> actual = storage.AsSpan(1, written);
                _ = strictUtf8.GetString(actual);
                Check(actual.SequenceEqual(expected.AsSpan(0, written)), "Diagnostic prefix changed");
                Check(truncated == (written == expected.Length ? 0 : 1), "Truncation flag incorrect");
                Check(written == capacity || storage[written + 1] == 0xA5, "Payload terminator/extra bytes written");
                if (truncated == 1)
                {
                    // The next scalar must not fit; no unnecessarily short prefix.
                    Rune next = Rune.GetRuneAt(strictUtf8.GetString(expected.AsSpan(written)), 0);
                    Check(next.Utf8SequenceLength > capacity - written, "A fitting scalar was omitted");
                }
            }
        }

        byte[] buffer = new byte[512];
        ManagedDiagnostic.Write(new BrokenException(), buffer, out int bytes, out int cut);
        Check(bytes == 0 && cut == 0, "Formatting exception escaped or stale metadata retained");
    }
}
