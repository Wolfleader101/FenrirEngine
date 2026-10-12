namespace Fenrir.Scripting;

/// <summary>The engine's clock, read when it is asked for.</summary>
public static class Time
{
    /// <summary>Seconds the last frame took.</summary>
    public static unsafe float DeltaTime => Native.TimeDeltaTime();

    /// <summary>Seconds between fixed steps (the Tick phase).</summary>
    public static unsafe float TickRate => Native.TimeTickRate();
}
