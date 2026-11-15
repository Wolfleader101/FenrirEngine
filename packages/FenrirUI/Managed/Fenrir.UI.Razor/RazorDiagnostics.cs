using Fenrir.Scripting;

namespace Fenrir.UI.Razor;

/// <summary>
/// Where the Razor runtime's warnings and errors go: the engine's log, or the sink a test sets, since a test runs with
/// no engine to log to.
/// </summary>
internal static class RazorDiagnostics
{
    /// <summary>Takes each message in place of the log, with whether it is an error; null logs.</summary>
    internal static Action<bool, string>? Sink { get; set; }

    /// <summary>Logs a warning, or hands it to the sink.</summary>
    public static void Warn(string message)
    {
        if (Sink != null)
            Sink(false, message);
        else
            Log.Warn(message);
    }

    /// <summary>Logs an error, or hands it to the sink.</summary>
    public static void Error(string message)
    {
        if (Sink != null)
            Sink(true, message);
        else
            Log.Error(message);
    }
}
