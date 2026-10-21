using Fenrir.Physics;
using Fenrir.Scripting;

namespace CompileFail;

// Nor any field of it.
public static class Driver
{
    public static void Use(Ref<VehicleWheel> wheel)
    {
        wheel.State.Compression = 0;
    }
}
