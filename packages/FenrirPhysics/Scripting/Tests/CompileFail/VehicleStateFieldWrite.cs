using Fenrir.Physics;
using Fenrir.Scripting;

namespace CompileFail;

// Nor any field of it.
public static class Driver
{
    public static void Use(Ref<Vehicle> vehicle)
    {
        vehicle.State.Gear = 2;
    }
}
