using Fenrir.Physics;
using Fenrir.Scripting;

namespace CompileFail;

// A driver holds Ref<Vehicle> to write Input, and reads State through the same reference, in place; a wheel's state
// likewise through Ref<VehicleWheel>.
public static class Driver
{
    public static bool Use(Ref<Vehicle> vehicle, Ref<VehicleWheel> wheel)
    {
        vehicle.Input.Forward = vehicle.State.ForwardSpeed < 10 ? 1 : 0;
        vehicle.Kind.Wheeled.Transmission.Mode = TransmissionMode.Manual;
        ref readonly VehicleState state = ref vehicle.State;
        ref readonly VehicleWheelState contact = ref wheel.State;
        return state.Shifting || contact.OnGround || !wheel.State.GroundCollider.IsNull;
    }

    public static float Look(ReadOnlyRef<Vehicle> vehicle) => vehicle.State.EngineRpm;
}
