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

    public static void Warn(string message)
    {
        if (Sink != null)
            Sink(false, message);
        else
            Log.Warn(message);
    }

    public static void Error(string message)
    {
        if (Sink != null)
            Sink(true, message);
        else
            Log.Error(message);
    }
}
