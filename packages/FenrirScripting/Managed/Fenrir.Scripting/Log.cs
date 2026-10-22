namespace Fenrir.Scripting;

/// <summary>Writes to the engine's logger, so script messages appear with everything else.</summary>
public static class Log
{
    private const int InfoLevel = 0, WarnLevel = 1, ErrorLevel = 2;

    public static void Info(string? message) => Write(InfoLevel, message);

    public static void Warn(string? message) => Write(WarnLevel, message);

    public static void Error(string? message) => Write(ErrorLevel, message);

    // The text goes across as UTF-8 and a length; a lone surrogate becomes U+FFFD rather than failing.
    private static void Write(int level, string? message) => Native.Log(level, message ?? "");
}
