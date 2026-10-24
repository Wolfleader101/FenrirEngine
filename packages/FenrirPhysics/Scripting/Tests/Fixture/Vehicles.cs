using Fenrir.Math;
using Fenrir.Physics;
using Fenrir.Scripting;

namespace Fenrir.PhysicsScriptFixture;

// Vehicles from a script. The tests call these through Iris, then play the changes back as the runner does after a
// system. Driving is field writes through Ref<Vehicle>, and both states are read in place: no native vehicle call.
public static class Vehicles
{
    // The wheels of the car BuildCar last made: front left, front right, rear left, rear right (entity order, so wheel
    // order).
    private static readonly uint[] wheels = new uint[4];

    // Gives the entity a `new Vehicle()`, with `new` of the kind numbered `kind` (as VehicleType numbers them)
    // assigned, or the vehicle's own new kind when `kind` is negative.
    public static void AddNewVehicle(uint entity, int kind)
    {
        var vehicle = new Vehicle();
        if (kind >= 0)
        {
            vehicle.Kind = (VehicleType)kind switch
            {
                VehicleType.Wheeled => new WheeledVehicle(),
                VehicleType.Motorcycle => new MotorcycleVehicle(),
                VehicleType.Tracked => new TrackedVehicle(),
                _ => new TrailerVehicle(),
            };
        }
        new Entity(entity).Add(vehicle);
    }

    public static void AddNewWheel(uint entity) => new Entity(entity).Add(new VehicleWheel());

    public static void AddNewDifferential(uint entity) => new Entity(entity).Add(new VehicleDifferential());

    public static void AddNewAntiRollBar(uint entity) => new Entity(entity).Add(new VehicleAntiRollBar());

    public static void AddDefaultVehicle(uint entity) => new Entity(entity).Add(default(Vehicle));

    public static void AddDefaultWheel(uint entity) => new Entity(entity).Add(default(VehicleWheel));

    // As a system builds a car: a 1500 kg unscaled box chassis 4 m long at (x, y, z) and four unscaled wheel entities,
    // not its children, linked to it, the front ones steering; with `rearDifferential`, a differential naming the rear
    // two. All made and linked in this one call. Returns the chassis.
    public static uint BuildCar(float x, float y, float z, bool rearDifferential)
    {
        var chassis = Entity.Create();
        chassis.Add(Placed(new Vector3(x, y, z)));
        chassis.Add(new RigidBody { Mass = 1500 });
        chassis.Add(new Collider(new BoxShape { HalfExtents = new Vector3(0.9f, 0.25f, 2) }));
        chassis.Add(new Vehicle());
        var made = new Entity[4];
        int index = 0;
        foreach (float wheelZ in new[] { -1.4f, 1.4f })
        {
            foreach (float wheelX in new[] { -0.9f, 0.9f })
            {
                var position = new Vector3(wheelX, -0.55f, wheelZ);
                var wheel = Entity.Create();
                wheel.Add(Placed(new Vector3(x, y, z) + position));
                wheel.Add(new VehicleWheel { Vehicle = chassis, Position = position, MaxSteerAngle = wheelZ < 0 ? 0.5f : 0 });
                made[index] = wheel;
                wheels[index++] = wheel.Value;
            }
        }
        if (rearDifferential)
        {
            var differential = Entity.Create();
            differential.Add(new VehicleDifferential { LeftWheel = made[2], RightWheel = made[3] });
        }
        return chassis.Value;
    }

    private static Transform Placed(Vector3 position) =>
        new() { Position = position, Rotation = Quaternion.Identity, Scale = Vector3.One };

    public static uint Wheel(int index) => wheels[index];

    public static void SetForward(uint chassis, float forward) =>
        new Entity(chassis).Get<Vehicle>().Input.Forward = forward;

    // Swaps the gearbox to Manual through the wheeled view, in place, and puts it in `gear`.
    public static void ShiftManual(uint chassis, int gear)
    {
        var vehicle = new Entity(chassis).Get<Vehicle>();
        vehicle.Kind.Wheeled.Transmission.Mode = TransmissionMode.Manual;
        vehicle.Input.Gear = gear;
    }

    // Turns the vehicle into a trailer, by assigning a new kind.
    public static void MakeTrailer(uint chassis) => new Entity(chassis).Get<Vehicle>().Kind = new TrailerVehicle();

    // Reads the vehicle's wheeled view, which must throw for any other kind: the exception's message, or null if it did
    // not.
    public static string? WheeledViewMessage(uint chassis)
    {
        try
        {
            _ = new Entity(chassis).Get<Vehicle>().Kind.Wheeled.LimitedSlipRatio;
            return null;
        }
        catch (InvalidOperationException exception)
        {
            return exception.Message;
        }
    }

    public static int KindType(uint chassis) => (int)new Entity(chassis).Get<Vehicle>().Kind.Type;

    public static void Disable(uint chassis) => new Entity(chassis).Get<Vehicle>().Enabled = false;

    public static void MakeKinematic(uint chassis) =>
        new Entity(chassis).Get<RigidBody>().MotionType = MotionType.Kinematic;

    // Read through Ref<Vehicle>, the reference Input is written through.
    public static float ForwardSpeed(uint chassis) => new Entity(chassis).Get<Vehicle>().State.ForwardSpeed;

    public static float EngineRpm(uint chassis) => new Entity(chassis).Get<Vehicle>().State.EngineRpm;

    public static int Gear(uint chassis) => new Entity(chassis).Get<Vehicle>().State.Gear;

    public static int WheelsOnGround(uint chassis) => new Entity(chassis).Get<Vehicle>().State.WheelsOnGround;

    public static bool OnGround(uint wheel) => new Entity(wheel).Get<VehicleWheel>().State.OnGround;

    public static uint GroundBody(uint wheel) => new Entity(wheel).Get<VehicleWheel>().State.GroundBody.Target.Value;

    public static uint GroundCollider(uint wheel) =>
        new Entity(wheel).Get<VehicleWheel>().State.GroundCollider.Target.Value;

    public static float Spin(uint wheel) => new Entity(wheel).Get<VehicleWheel>().State.AngularVelocity;

    // Whether the vehicle's state, read in place, is a new VehicleState, field by field.
    public static bool StateIsNew(uint chassis)
    {
        ref readonly VehicleState state = ref new Entity(chassis).Get<Vehicle>().State;
        return state.ForwardSpeed == 0 && state.EngineRpm == 0 && state.Gear == 0 && state.Clutch == 0 &&
               !state.Shifting && state.WheelsOnGround == 0 && state.LeanAngle == 0 && state.LeftTrackSpeed == 0 &&
               state.RightTrackSpeed == 0;
    }

    // Whether the wheel's state, read in place, is a new VehicleWheelState, field by field.
    public static bool WheelStateIsNew(uint wheel)
    {
        ref readonly VehicleWheelState state = ref new Entity(wheel).Get<VehicleWheel>().State;
        return !state.OnGround && state.GroundBody.IsNull && state.GroundCollider.IsNull &&
               state.ContactPoint == Vector3.Zero && state.ContactNormal == Vector3.Zero &&
               state.GroundVelocity == Vector3.Zero && state.Compression == 0 && state.SuspensionForce == 0 &&
               state.LongitudinalSlip == 0 && state.LateralSlip == 0 && state.AngularVelocity == 0 &&
               state.SteerAngle == 0;
    }
}
