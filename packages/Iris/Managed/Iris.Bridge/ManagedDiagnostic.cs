using System;
using System.Text;

namespace Iris.Bridge;

internal static class ManagedDiagnostic
{
    // Clear all available metadata even when another argument is invalid.
    internal static unsafe bool ValidateOutput(byte* destination, int capacity, int* written, int* truncated)
    {
        if (written != null) *written = 0;
        if (truncated != null) *truncated = 0;
        return written != null && truncated != null && capacity >= 0 && (destination != null || capacity == 0);
    }

    // Called with validated storage from inside an export's catch block. Reporting
    // must itself contain ordinary exceptions, including user-overridden ToString.
    internal static void Write(Exception exception, Span<byte> destination, out int written, out int truncated)
    {
        written = 0;
        truncated = 0;
        try
        {
            foreach (Rune rune in exception.ToString().EnumerateRunes())
            {
                if (rune.Utf8SequenceLength > destination.Length - written)
                {
                    truncated = 1;
                    break;
                }
                written += rune.EncodeToUtf8(destination.Slice(written));
            }
        }
        catch (Exception)
        {
            // Discard even partially written data. Native code uses its fallback.
            written = 0;
            truncated = 0;
        }
    }
}
