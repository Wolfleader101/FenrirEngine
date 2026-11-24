namespace Fenrir.Scripting;

/// <summary>The app the scripts run in.</summary>
public static class App
{
    /// <summary>Stops the app: the frame being run finishes, and no other begins.</summary>
    public static unsafe void Stop() => Native.AppStop();
}
