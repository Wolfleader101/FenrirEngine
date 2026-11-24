namespace Fenrir.Scripting;

/// <summary>The engine's clock, read when it is asked for.</summary>
public static class Time
{
    /// <summary>Seconds the last frame took.</summary>
    public static unsafe float DeltaTime => Native.TimeDeltaTime();

    /// <summary>Seconds between fixed steps (the Tick phase).</summary>
    public static unsafe float TickRate => Native.TimeTickRate();

    /// <summary>Seconds since the app started, read from the clock each time it is asked for.</summary>
    public static unsafe double Elapsed => Native.TimeElapsed();

    /// <summary>The frames begun so far: during a frame, its number, from 1.</summary>
    public static unsafe ulong FrameCount => Native.TimeFrameCount();

    /// <summary>The fixed steps begun so far: during a step, its number, from 1; through a frame with no step, the last
    /// step's.</summary>
    public static unsafe ulong TickCount => Native.TimeTickCount();
}
