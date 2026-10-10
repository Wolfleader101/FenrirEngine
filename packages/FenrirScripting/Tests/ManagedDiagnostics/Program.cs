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

        // Call the actual production UCO export through its unmanaged ABI.
        delegate* unmanaged[Cdecl]<int, int, int*, byte*, int, int*, int*, int> add = &Bootstrap.TryAddInt32;
        fixed (byte* destination = buffer)
        {
            int value = 99, written = 99, truncated = 99;
            Check(add(20, 22, &value, destination, buffer.Length, &written, &truncated) == 0 &&
                  value == 42 && written == 0 && truncated == 0, "Success did not clear outputs");
            for (int capacity = 0; capacity <= 16; ++capacity)
            {
                Array.Fill(buffer, (byte)0xA5);
                Check(add(int.MaxValue, 1, &value, destination + 1, capacity, &written, &truncated) == 2 &&
                      value == 0 && written <= capacity && truncated == 1, "Overflow contract changed");
                Check(buffer[0] == 0xA5 && buffer[capacity + 1] == 0xA5, "Export wrote outside capacity");
                _ = strictUtf8.GetString(buffer.AsSpan(1, written));
            }
            Check(add(int.MaxValue, 1, &value, null, 0, &written, &truncated) == 2 && written == 0 && truncated == 1,
                  "Absent diagnostic buffer changed operation status");
            Check(add(1, 2, &value, null, 1, &written, &truncated) == 1 && value == 0 && written == 0 && truncated == 0,
                  "Null buffer with capacity accepted");
            Check(add(1, 2, &value, destination, -1, &written, &truncated) == 1, "Negative capacity accepted");
            Check(add(1, 2, null, destination, 1, &written, &truncated) == 1, "Null result accepted");
            Check(add(1, 2, &value, destination, 1, null, &truncated) == 1, "Null count accepted");
            Check(add(1, 2, &value, destination, 1, &written, null) == 1, "Null flag accepted");
            Check(add(7, -3, &value, destination, buffer.Length, &written, &truncated) == 0 &&
                  value == 4 && written == 0 && truncated == 0, "Recovery retained diagnostics");
        }
        delegate* unmanaged[Cdecl]<byte*, int, int*, byte*, int, int*, int*, int> greeting = &Bootstrap.CopyGreetingUtf8;
        delegate* unmanaged[Cdecl]<byte*, int, byte*, int, int*, byte*, int, int*, int*, int> echo = &Bootstrap.EchoUtf8;
        byte[] payload = new byte[64];
        byte[] malformed = { 0xC3, (byte)'(' };
        byte[] valid = { (byte)'A', 0, (byte)'B' };
        fixed (byte* destination = buffer, output = payload, invalidInput = malformed, validInput = valid)
        {
            int required = 99, written = 99, truncated = 99;
            Check(greeting(null, 0, &required, destination, buffer.Length, &written, &truncated) == 0 &&
                  required == 27 && written == 0 && truncated == 0, "Greeting query diagnostic contract changed");
            for (int capacity = 0; capacity < 27; ++capacity)
            {
                Array.Fill(payload, (byte)0xA5);
                Check(greeting(output + 1, capacity, &required, destination, buffer.Length, &written, &truncated) == 4 &&
                      required == 27 && written == 0 && truncated == 0, "Small greeting buffer changed status");
                Check(Array.TrueForAll(payload, item => item == 0xA5), "Small buffer wrote partial greeting");
            }
            Check(greeting(output + 1, 27, &required, destination, buffer.Length, &written, &truncated) == 0 &&
                  required == 27 && written == 0 && truncated == 0 && payload[0] == 0xA5 && payload[28] == 0xA5,
                  "Greeting copy bounds or diagnostics incorrect");
            Check(strictUtf8.GetString(payload.AsSpan(1, 27)) == "Hello from .NET \u2014 \u03C0 \U0001F43A", "Greeting payload changed");

            for (int capacity = 0; capacity <= 64; ++capacity)
            {
                Array.Fill(buffer, (byte)0xA5);
                Array.Fill(payload, (byte)0xA5);
                Check(echo(invalidInput, 2, output + 1, 3, &required, destination + 1, capacity, &written, &truncated) == 1 &&
                      required == 0 && written <= capacity && truncated == 1, "Decoder failure contract changed");
                Check(buffer[0] == 0xA5 && buffer[capacity + 1] == 0xA5 && Array.TrueForAll(payload, item => item == 0xA5),
                      "Decoder failure wrote payload or exceeded diagnostic capacity");
                _ = strictUtf8.GetString(buffer.AsSpan(1, written));
            }
            Check(echo(invalidInput, 2, null, 0, &required, destination, buffer.Length, &written, &truncated) == 1 &&
                  required == 0 && strictUtf8.GetString(buffer.AsSpan(0, written)).Contains("DecoderFallbackException"),
                  "Failed query omitted decoder diagnostic");
            Check(echo(invalidInput, 2, null, 0, &required, null, 0, &written, &truncated) == 1 && written == 0 && truncated == 1,
                  "Zero diagnostic capacity changed decoder status");
            Check(echo(validInput, 3, null, 0, &required, destination, buffer.Length, &written, &truncated) == 0 &&
                  required == 3 && written == 0 && truncated == 0, "Successful echo query retained error");
            Array.Fill(payload, (byte)0xA5);
            Check(echo(validInput, 3, output + 1, 2, &required, destination, buffer.Length, &written, &truncated) == 4 &&
                  required == 3 && written == 0 && truncated == 0 && Array.TrueForAll(payload, item => item == 0xA5),
                  "Small echo buffer exposed partial output");
            Check(echo(validInput, 3, output + 1, 3, &required, destination, buffer.Length, &written, &truncated) == 0 &&
                  payload.AsSpan(1, 3).SequenceEqual(valid) && written == 0 && truncated == 0 && payload[4] == 0xA5,
                  "Successful echo copy retained error or changed embedded null");

            Check(greeting(null, 0, &required, null, 1, &written, &truncated) == 1 && required == 0 && written == 0 && truncated == 0,
                  "Greeting accepted null diagnostic storage with capacity");
            Check(greeting(null, 0, &required, destination, -1, &written, &truncated) == 1 && required == 0,
                  "Greeting accepted negative diagnostic capacity");
            Check(greeting(null, 0, null, destination, 1, &written, &truncated) == 1 && written == 0 && truncated == 0,
                  "Greeting accepted null payload count");
            Check(greeting(null, 0, &required, destination, 1, null, &truncated) == 1 && required == 0 && truncated == 0,
                  "Greeting accepted null diagnostic count");
            Check(greeting(null, 0, &required, destination, 1, &written, null) == 1 && required == 0 && written == 0,
                  "Greeting accepted null diagnostic flag");
            Check(echo(validInput, 3, null, 0, &required, null, 1, &written, &truncated) == 1 && required == 0,
                  "Echo accepted null diagnostic storage with capacity");
            Check(echo(validInput, 3, null, 0, &required, destination, -1, &written, &truncated) == 1 && required == 0,
                  "Echo accepted negative diagnostic capacity");
            Check(echo(validInput, 3, null, 0, &required, destination, 1, null, &truncated) == 1 && required == 0,
                  "Echo accepted null diagnostic count");
            Check(echo(validInput, 3, null, 0, &required, destination, 1, &written, null) == 1 && required == 0,
                  "Echo accepted null diagnostic flag");
            Check(echo(validInput, 3, null, 0, null, destination, 1, &written, &truncated) == 1,
                  "Echo accepted null payload count");
            Check(echo(null, 1, null, 0, &required, destination, 1, &written, &truncated) == 1 && required == 0,
                  "Echo accepted null input with nonzero length");
            Check(echo(null, 0, null, 0, &required, null, 0, &written, &truncated) == 0 && required == 0 && written == 0 && truncated == 0,
                  "Empty echo/query without diagnostic buffer failed");
        }
        Console.WriteLine("Managed diagnostics: UTF-8 boundaries, canaries, formatting failure, integer/string ABI validation and recovery passed.");
        Check(args.Length == 1, "Expected the component fixture path");
        ComponentChecks.Run(args[0]);
        InvocationChecks.Run(args[0]);
        DependencyChecks.Run(args[0]);
        fixed (byte* destination = buffer)
        {
            int value = 99, written = 99, truncated = 99;
            Check(add(20, 22, &value, destination, buffer.Length, &written, &truncated) == 0 &&
                  value == 42 && written == 0 && truncated == 0,
                  "Resident bridge did not recover after dependency resolution/release checks");
        }
    }
}
