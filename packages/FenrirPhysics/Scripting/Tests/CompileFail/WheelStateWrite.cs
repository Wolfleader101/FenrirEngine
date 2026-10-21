using Fenrir.Physics;
using Fenrir.Scripting;

namespace CompileFail;

// A wheel's state is overwritten by physics after every step too, so C# can never write it.
public static class Driver
{
    public static void Use(Ref<VehicleWheel> wheel)
    {
        wheel.State = new VehicleWheelState();
    }
}
