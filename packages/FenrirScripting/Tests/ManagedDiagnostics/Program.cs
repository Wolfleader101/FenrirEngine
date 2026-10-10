using System;
using System.Text;
using Fenrir.ManagedHost;

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

    static unsafe void Main(string[] args)
    {
        if (args.Length == 2 && args[0] == "--unload")
        {
            UnloadChecks.Run(args[1]);
            return;
        }
        HandshakeChecks.Run();
        var strictUtf8 = new UTF8Encoding(false, true);
        // Every possible boundary, including capacities too small for the first scalar.
        foreach (string text in new[] { "", "ASCII", "\u03C0\U0001F43A\u20AC!", "A\0B", "\uD800X" })
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

        Console.WriteLine("Managed diagnostics: UTF-8 boundaries, canaries and formatting failure passed.");
        Check(args.Length == 1, "Expected the component fixture path");
        ComponentChecks.Run(args[0]);
        InvocationChecks.Run(args[0]);
        DependencyChecks.Run(args[0]);
        InvocationChecks.CheckResidentBridge(args[0]);
    }
}
