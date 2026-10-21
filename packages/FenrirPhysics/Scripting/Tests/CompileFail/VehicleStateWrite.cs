using Fenrir.Physics;
using Fenrir.Scripting;

namespace CompileFail;

// Physics overwrites the state after every step, so C# can never write it.
public static class Driver
{
    public static void Use(Ref<Vehicle> vehicle)
    {
        vehicle.State = new VehicleState();
    }
}
