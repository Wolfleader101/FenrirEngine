using Fenrir.Scripting;

namespace CompileFail;

// A writable component with a part the engine owns: the same Ref<T> writes Speed and reads Reading in place, and the
// read-only view reads both.
public readonly struct Reading
{
    public readonly int Level;
}

public struct Gauge : IComponent
{
    public int Speed;
    public readonly Reading Reading;
    public readonly int Samples;
}

public static class UseGauge
{
    public static int Use(Ref<Gauge> gauge)
    {
        gauge.Speed = gauge.Reading.Level + gauge.Samples;
        ref readonly Reading reading = ref gauge.Reading;
        return reading.Level;
    }

    public static int Look(ReadOnlyRef<Gauge> gauge) => gauge.Speed + gauge.Reading.Level + gauge.Samples;
}
