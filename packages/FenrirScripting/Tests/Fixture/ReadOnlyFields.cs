using System.Runtime.CompilerServices;
using Fenrir.Scripting;

namespace Fenrir.ScriptFixture;

// The C# mirror of a native component with a part only C++ writes (the test's TestNative::Gauge), shaped like a character
// body and its state: scripts write Rate and read Reading through the same Ref<T>, but cannot write Reading.
public readonly struct GaugeReading
{
    public readonly float Level;
    public readonly int Samples;
}

[NativeComponent("Test.Gauge")]
public struct Gauge : IComponent
{
    public float Rate;
    public readonly GaugeReading Reading;
}

// What GaugeSystem saw, for the tests. The system does nothing until a test calls Enable (Test.Gauge is registered only by
// the read-only field test).
public static class GaugeProbe
{
    public static bool Enabled;

    private static float seenLevel = -1;
    private static int seenSamples = -1;
    private static ulong seenAddress;

    public static void Enable() => Enabled = true;

    public static float SeenLevel() => seenLevel;

    public static int SeenSamples() => seenSamples;

    // Where the reading the system read lives, so the test can check it was the component's storage and not a copy.
    public static ulong SeenAddress() => seenAddress;

    internal static unsafe void Saw(in GaugeReading reading)
    {
        seenLevel = reading.Level;
        seenSamples = reading.Samples;
        seenAddress = (ulong)Unsafe.AsPointer(ref Unsafe.AsRef(in reading));
    }
}

// Reads the reading C++ wrote through the writable Ref<Gauge> the query hands out, in place, and writes the rate from it.
[System(Phase.Update)]
public sealed class GaugeSystem : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        if (!GaugeProbe.Enabled)
            return;
        foreach (var (entity, gauge) in context.Query<Gauge>())
        {
            GaugeProbe.Saw(in gauge.Reading);
            gauge.Rate = gauge.Reading.Level * gauge.Reading.Samples;
        }
    }
}
