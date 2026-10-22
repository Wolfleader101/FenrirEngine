using Fenrir.Math;
using Fenrir.Physics;
using Fenrir.Scripting;

namespace DemoScripts;

// A VEHICLES SCENE MADE AND RUN ONLY FROM C#
//
// F6 steps the demo from Default through the C++ vehicle scenes to this one, "Vehicles (C#)". C++ made it empty but for
// one entity carrying a CSharpScene marker with this scene's id (examples/DemoApp/src/CSharpScenes.hpp); everything else
// is made here, from Fenrir.Physics's vehicle components, in one system:
//
//   The car: a dynamic RigidBody with an unscaled box Collider and a Vehicle (a WheeledVehicle, new), and four unscaled
//     wheel entities, each a VehicleWheel naming the chassis by EntityLink, not its children. A VehicleDifferential on
//     an entity of its own drives the rear two, and a VehicleAntiRollBar across each axle keeps it level in corners.
//   The trailer: a 300 kg TrailerVehicle on one axle, hitched to the car by an ordinary ball Joint. Nothing links their
//     brakes: the driver system copies the car's Brake and Handbrake into the trailer's Input every tick.
//   A small track on tarmac (friction 0.5): five bumps north of the start, a ramp up to 2 m and a drop, and a grass
//     patch (friction 0.3) on the way back south, which the wheels' readouts name through their GroundCollider.
//
// Driving is field writes through Ref<Vehicle> in a Tick system: Input.Forward, Steer, Brake, Handbrake and Clutch, and
// Input.Gear in Manual. M swaps the gearbox through the kind's view (Kind.Wheeled.Transmission.Mode), and E/Q shift.
// The state is read in place, through the same reference, for the readouts and the autopilot. There are no vehicle
// native functions.
//
// C# cannot parent one entity to another, so each look (the chassis's box, each wheel's tyre and stripe, the trailer's
// tongue) is an entity of its own that the readings system moves onto what it shows every frame, as the character
// scene's look is. The chassis and wheel entities stay unscaled.
//
// The camera is a chase camera system writing the scene's DemoCamera, which a C++ system copies into the app's camera.
// A gamepad drives too, read from the scene's DemoGamepad, with the C++ scenes' deadzone and mapping.
//
//   W / S, arrows  throttle; S brakes, then reverses once stopped (only the arrows while the camera is free)
//   A / D          steer
//   Space          handbrake
//   Shift          hold the clutch
//   C              free the camera (WASD flies it) and snap it back
//   T              teleport the car and its trailer to the start
//   R / K          right the car (a Transform write, upright and lifted 1 m, its velocities zeroed) / kick it over
//   M, E / Q       Manual gearbox through Kind.Wheeled, shift up / down
//   V              the vehicles' debug lines on and off, through PhysicsWorld.SetDebugDraw (F2 or a scene switch takes
//                  the lines back to the app's mode)
//   U              the trailer turned into a wheeled car by assigning a new VehicleKind (it then pushes, given the car's
//                  Forward), and back into a trailer
//   L              log the car's whole state and each wheel's
//   P              the autopilot, which drives a lap of the track; off, it brakes the car to a stop, until the
//                  driver drives
//
// The car's, the trailer's and each wheel's names show their readings every frame, in the C++ scenes' format. As in the
// other C# scenes, the scene's state lives in components, so it waits while another scene is shown and survives a hot
// reload.

// The car's driver: what the keys, the pad and the autopilot ask of it, and what it remembers between ticks.
public struct VehicleDriver : IComponent
{
    public int Held;           // The driving keys held, one VehiclesScene.Hold bit each, keyboard and pad together.
    public bool Autopilot;
    public bool Parking;       // Since the autopilot let go: braked until the driver drives.
    public int Waypoint;
    public float Direction;    // The way the keys last drove it: S brakes while it goes forward, then reverses.
    public bool Manual;
    public bool DrawVehicles;  // V.
    public bool TrailerDriven; // U: the trailer is a wheeled car.
    public bool Kick;          // K, until the next tick.
    public bool Right;         // R, until the next tick.
    public float Upturned;     // Seconds with no wheel on the ground and the chassis's up more than 60° from world up.
    public int Tick;
    public Entity Trailer;
    public Entity Wheel0, Wheel1, Wheel2, Wheel3; // Front left, front right, rear left, rear right: wheel order.
}

// A look: an entity drawn in the place of another, at its pose and `Offset` from it in its axes, every frame.
public struct VehicleLook : IComponent
{
    public Entity Target;
    public Vector3 Offset;
}

public static class VehiclesScene
{
    // This scene's id in its CSharpScene marker, as C++ gives it (CSharpScenes::vehiclesId).
    public const int SceneId = 4;

    // GLFW's key codes.
    public const int KeySpace = 32;
    public const int KeyA = 65;
    public const int KeyC = 67;
    public const int KeyD = 68;
    public const int KeyE = 69;
    public const int KeyK = 75;
    public const int KeyL = 76;
    public const int KeyM = 77;
    public const int KeyP = 80;
    public const int KeyQ = 81;
    public const int KeyR = 82;
    public const int KeyS = 83;
    public const int KeyT = 84;
    public const int KeyU = 85;
    public const int KeyV = 86;
    public const int KeyW = 87;
    public const int KeyRight = 262;
    public const int KeyLeft = 263;
    public const int KeyDown = 264;
    public const int KeyUp = 265;
    public const int KeyLeftShift = 340;
    public const int KeyRightShift = 344;

    // VehicleDriver.Held's bits, for the keys that act while held.
    [Flags]
    public enum Hold
    {
        None = 0,
        W = 1 << 0,
        A = 1 << 1,
        S = 1 << 2,
        D = 1 << 3,
        Up = 1 << 4,
        Left = 1 << 5,
        Down = 1 << 6,
        Right = 1 << 7,
        Shift = 1 << 8,
        Space = 1 << 9,
    }

    public static Hold HoldOf(int key) => key switch
    {
        KeyW => Hold.W,
        KeyA => Hold.A,
        KeyS => Hold.S,
        KeyD => Hold.D,
        KeyUp => Hold.Up,
        KeyLeft => Hold.Left,
        KeyDown => Hold.Down,
        KeyRight => Hold.Right,
        KeyLeftShift or KeyRightShift => Hold.Shift,
        KeySpace => Hold.Space,
        _ => Hold.None,
    };

    public const float Deadzone = 0.15f;  // A pad's trigger or stick axis this close to rest leaves the keys be.
    public const float StoppedSpeed = 0.5f; // m/s: below it, S reverses rather than brakes.
    public const float Degrees = MathF.PI / 180;

    // The car's place on the start pad, its tow hitch 0.3 m behind its chassis, and the trailer's place behind that.
    public static readonly Vector3 Start = new(0, 1, 0);
    public static readonly Vector3 Hitch = new(0, -0.1f, 2.3f);
    public static readonly Vector3 TrailerOffset = new(0, 0, 2.3f + 1.9f);

    // Whether the active scene is this one, built.
    public static bool IsActive(in SystemContext context)
    {
        foreach (var (entity, marker) in context.Query<CSharpScene>())
            if (marker.Scene == SceneId && marker.Built != 0)
                return true;
        return false;
    }

    public static Quaternion AngleAxis(float angle, Vector3 unitAxis)
    {
        float sine = MathF.Sin(angle / 2);
        return new Quaternion(unitAxis.X * sine, unitAxis.Y * sine, unitAxis.Z * sine, MathF.Cos(angle / 2));
    }

    // `value` turned by the unit quaternion `rotation`.
    public static Vector3 Rotate(Quaternion rotation, Vector3 value)
    {
        var axis = new Vector3(rotation.X, rotation.Y, rotation.Z);
        Vector3 twice = Cross(axis, value) * 2;
        return value + twice * rotation.W + Cross(axis, twice);
    }

    public static Vector3 Cross(Vector3 a, Vector3 b) =>
        new(a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X);

    // Turned about world up only, facing the way `rotation` faces on the ground plane.
    public static Quaternion Upright(Quaternion rotation)
    {
        Vector3 ahead = Rotate(rotation, new Vector3(0, 0, -1));
        return AngleAxis(MathF.Atan2(-ahead.X, -ahead.Z), new Vector3(0, 1, 0));
    }

    public static string NameOf(EntityLink link) => link.IsNull ? "none" : NameOf(link.Target);

    public static string NameOf(Entity entity) => entity.IsValid ? entity.Get<Name>().Value.GetText() : "?";

    public static string Format(Vector3 value) => $"({value.X:0.00}, {value.Y:0.00}, {value.Z:0.00})";

    // An entity drawn as `kind` in `colour`, `size` across. Its components land when the building system returns.
    public static Entity Box(string text, Vector3 position, Vector3 size, Vector3 colour, Quaternion? rotation = null,
                             MeshKind kind = MeshKind.Cube)
    {
        Entity entity = Named(text, position, rotation ?? Quaternion.Identity, size);
        entity.Add(new DemoMesh { Kind = kind, Colour = colour });
        return entity;
    }

    // An entity with only a name and a Transform.
    public static Entity Named(string text, Vector3 position, Quaternion rotation, Vector3 scale)
    {
        Entity entity = Entity.Create();
        var name = new Name();
        name.SetText(text);
        entity.Add(name);
        entity.Add(new Transform { Position = position, Rotation = rotation, Scale = scale });
        return entity;
    }

    // A look drawn in `target`'s place, `offset` from it in its axes.
    public static void Look(string text, Entity target, Vector3 size, Vector3 colour, Vector3 offset = default,
                            MeshKind kind = MeshKind.Cube)
    {
        // Placed by VehiclesReadings from the first frame on.
        Box(text, Vector3.Zero, size, colour, kind: kind)
            .Add(new VehicleLook { Target = target, Offset = offset });
    }

    // T: the car and its trailer back at the start, facing north and still. A Transform write is a teleport: the
    // engine's speed and the wheels' spin carry on, and physics poses the wheels on the chassis at once.
    public static void Respawn(Entity car, Entity trailer)
    {
        Place(car, Start, Quaternion.Identity);
        Place(trailer, Start + TrailerOffset, Quaternion.Identity);
    }

    // A body put at `position`, turned by `rotation`, its velocities zeroed.
    public static void Place(Entity body, Vector3 position, Quaternion rotation)
    {
        ref Transform transform = ref body.Get<Transform>().Value;
        transform.Position = position;
        transform.Rotation = rotation;
        body.Get<RigidBody>().LinearVelocity = Vector3.Zero;
        body.Get<RigidBody>().AngularVelocity = Vector3.Zero;
    }
}

// Builds the scene the first time it finds its marker unbuilt: everything, in this one system.
[System(Phase.Update, Order = 400)]
public sealed class VehiclesBuilder : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        foreach (var (entity, marker) in context.Query<CSharpScene>())
        {
            if (marker.Scene != VehiclesScene.SceneId || marker.Built != 0)
                continue;
            marker.Built = 1; // Written into the ECS now, so the scene is built once, even across a reload.
            Build(in context);
        }
    }

    private static void Build(in SystemContext context)
    {
        BuildTrack();
        Entity car = BuildCar(out Entity[] wheels);
        Entity trailer = BuildTrailer(car);
        car.Add(new VehicleDriver
        {
            Direction = 1,
            Trailer = trailer,
            Wheel0 = wheels[0],
            Wheel1 = wheels[1],
            Wheel2 = wheels[2],
            Wheel3 = wheels[3],
        });

        // Where the chase camera writes the camera it wants, behind the car looking north.
        VehiclesScene.Named("Vehicle camera", Vector3.Zero, Quaternion.Identity, Vector3.One)
            .Add(new DemoCamera { Yaw = -90, Pitch = -15, Active = 1 });
        // Where C++ writes the first connected gamepad each frame.
        VehiclesScene.Named("Gamepad", Vector3.Zero, Quaternion.Identity, Vector3.One).Add(new DemoGamepad());

        // Its keys are logged by the scene switcher on entry (CSharpScenes::Vehicles).
        context.Log.Info($"Vehicles (C#): C# built a track of bumps, a ramp and a grass patch, a car ({car}) on four " +
                         $"linked wheels with a rear differential and two anti-roll bars, and its trailer ({trailer}) " +
                         $"on a ball joint.");
    }

    // Tarmac all round with a white start pad at the origin, facing north (-Z); five bumps on the start straight; a
    // ramp rising north to 2 m, then a drop; and east of the start a grass patch, a thin static slab whose
    // PhysicsMaterial the wheels on it grip by.
    private static void BuildTrack()
    {
        // 200 m square: wide enough round the lap that a car let go at speed rolls to rest on it.
        Entity tarmac = VehiclesScene.Box("Tarmac", new Vector3(0, -0.5f, -40), new Vector3(200, 1, 200),
            new Vector3(0.35f, 0.35f, 0.35f));
        tarmac.Add(new Collider());
        tarmac.Add(new PhysicsMaterial { Friction = 0.5f });
        VehiclesScene.Box("Start", new Vector3(0, 0.005f, 0), new Vector3(4, 0.01f, 6), new Vector3(0.95f, 0.95f, 0.95f));

        // Square bars turned 45° about their length, 0.14 m high, every 2 m.
        Quaternion diamond = VehiclesScene.AngleAxis(MathF.PI / 4, new Vector3(1, 0, 0));
        for (int i = 0; i < 5; ++i)
            VehiclesScene.Box("Bump", new Vector3(0, 0, -20 - 2 * i), new Vector3(8, 0.2f, 0.2f),
                new Vector3(0.8f, 0.7f, 0.2f), diamond).Add(new Collider());

        // 10 m long and 6 m wide.
        Quaternion rise = VehiclesScene.AngleAxis(MathF.Asin(2.0f / 10), new Vector3(1, 0, 0));
        VehiclesScene.Box("Ramp", new Vector3(0, 0.75f, -50), new Vector3(6, 0.5f, 10), new Vector3(0.6f, 0.45f, 0.3f),
            rise).Add(new Collider());

        Entity grass = VehiclesScene.Box("Grass", new Vector3(25, 0.01f, -35), new Vector3(8, 0.02f, 50),
            new Vector3(0.3f, 0.6f, 0.2f));
        grass.Add(new Collider());
        grass.Add(new PhysicsMaterial { Friction = 0.3f });
    }

    // A 1500 kg car 4 m long on the start pad, its front wheels steering, driven at the rear.
    private static Entity BuildCar(out Entity[] wheels)
    {
        Vector3 at = VehiclesScene.Start;
        Entity car = Chassis("Car", at, new Vector3(0.9f, 0.25f, 2), 1500, new Vector3(0.85f, 0.15f, 0.1f));
        car.Add(new Vehicle());
        wheels = new Entity[4];
        int index = 0;
        foreach (float z in new[] { -1.4f, 1.4f })
            foreach (float x in new[] { -0.9f, 0.9f })
                wheels[index++] = Wheel(new VehicleWheel
                {
                    Vehicle = car,
                    Position = new Vector3(x, -0.55f, z),
                    MaxSteerAngle = z < 0 ? 0.5f : 0,
                }, at);

        // A differential naming the rear wheels replaces the all-wheel-drive default; an anti-roll bar across each axle.
        Entity differential = VehiclesScene.Named("Car rear differential", Vector3.Zero, Quaternion.Identity, Vector3.One);
        differential.Add(new VehicleDifferential { LeftWheel = wheels[2], RightWheel = wheels[3] });
        foreach (int left in new[] { 0, 2 })
        {
            Entity bar = VehiclesScene.Named(left == 0 ? "Car front anti-roll bar" : "Car rear anti-roll bar",
                Vector3.Zero, Quaternion.Identity, Vector3.One);
            bar.Add(new VehicleAntiRollBar { LeftWheel = wheels[left], RightWheel = wheels[left + 1] });
        }
        return car;
    }

    // The trailer: a 300 kg TrailerVehicle 2 m long behind the car, on one axle at its middle, with a tongue to the
    // car's tow hitch. The hitch is a ball Joint on its own entity: with CollideConnected false the two never fight,
    // and they sleep and wake together. Both bodies are unturned and unscaled, so its frames are the hitch's offset
    // from each.
    private static Entity BuildTrailer(Entity car)
    {
        Vector3 at = VehiclesScene.Start + VehiclesScene.TrailerOffset;
        Entity trailer = Chassis("Trailer", at, new Vector3(0.8f, 0.3f, 1), 300, new Vector3(0.3f, 0.35f, 0.8f));
        trailer.Add(new Vehicle { Kind = new TrailerVehicle() });
        foreach (float x in new[] { -0.85f, 0.85f })
            Wheel(new VehicleWheel { Vehicle = trailer, Position = new Vector3(x, -0.45f, 0) }, at);
        VehiclesScene.Look("Tongue", trailer, new Vector3(0.1f, 0.1f, 1.3f), new Vector3(0.2f, 0.2f, 0.2f),
            new Vector3(0, -0.1f, -1.6f));

        Vector3 hitch = VehiclesScene.Start + VehiclesScene.Hitch;
        Entity joint = VehiclesScene.Named("Hitch", hitch, Quaternion.Identity, Vector3.One);
        joint.Add(new Joint
        {
            BodyA = car,
            BodyB = trailer,
            FrameA = new JointFrame { Position = hitch - VehiclesScene.Start },
            FrameB = new JointFrame { Position = hitch - at },
            Kind = new BallJoint(),
        });
        return trailer;
    }

    // A chassis: unscaled, with a box collider of `half` extents, and its look, the box scaled to size.
    private static Entity Chassis(string text, Vector3 at, Vector3 half, float mass, Vector3 colour)
    {
        Entity chassis = VehiclesScene.Named(text, at, Quaternion.Identity, Vector3.One);
        chassis.Add(new RigidBody { Mass = mass });
        chassis.Add(new Collider(new BoxShape { HalfExtents = half }));
        VehiclesScene.Look("Chassis look", chassis, half * 2, colour);
        return chassis;
    }

    // A wheel entity at its place on the chassis made at `at`: unscaled, posed by physics, not the chassis's child.
    // Its look is a sphere scaled to the tyre, with a white stripe across it to show its spin.
    private static Entity Wheel(VehicleWheel settings, Vector3 at)
    {
        Entity wheel = VehiclesScene.Named("Wheel", at + settings.Position, Quaternion.Identity, Vector3.One);
        wheel.Add(settings);
        float diameter = 2 * settings.Radius;
        VehiclesScene.Look("Tyre", wheel, new Vector3(settings.Width, diameter, diameter), new Vector3(0.1f, 0.1f, 0.1f),
            kind: MeshKind.Sphere);
        VehiclesScene.Look("Stripe", wheel, new Vector3(1.05f * settings.Width, 0.25f * diameter, 0.9f * diameter),
            new Vector3(0.95f, 0.95f, 0.95f));
        return wheel;
    }
}

// The scene's keys and the pad's buttons. Presses act at once, or ask the next tick to; the held keys are kept for the
// driver.
[System(Phase.Update, Order = 410)]
public sealed class VehiclesInput : ISystem
{
    // A pad button and the key it presses: the C++ scenes' mapping, for this scene's keys.
    private static readonly (GamepadButton Button, int Key)[] PadKeys =
    [
        (GamepadButton.A, VehiclesScene.KeySpace),
        (GamepadButton.B, VehiclesScene.KeyLeftShift),
        (GamepadButton.X, VehiclesScene.KeyM),
        (GamepadButton.LeftBumper, VehiclesScene.KeyQ),
        (GamepadButton.RightBumper, VehiclesScene.KeyE),
        (GamepadButton.Back, VehiclesScene.KeyR),
    ];

    private readonly EventReader<KeyboardKey> keys = new();
    // Kept here too, so a key released while another scene is shown is not held when this one comes back.
    private VehiclesScene.Hold held;
    private VehiclesScene.Hold padHeld;
    private uint lastButtons;

    public void OnUpdate(in SystemContext context)
    {
        // Read in every scene, so a key pressed in another scene is not acted on here later.
        bool active = VehiclesScene.IsActive(in context);
        foreach (var key in keys.Read())
        {
            if (key.State == InputState.Released)
            {
                held &= ~VehiclesScene.HoldOf(key.Key);
                continue;
            }
            if (!active || key.State != InputState.Pressed)
                continue;
            held |= VehiclesScene.HoldOf(key.Key);
            OnKey(in context, key.Key);
        }
        if (!active)
        {
            // C++ writes only the active scene's pad: let go of what it held, so nothing is stuck on return.
            padHeld = VehiclesScene.Hold.None;
            lastButtons = 0;
            return;
        }

        // A button just pressed presses its key, as the keyboard's would; one let go (or unplugged) releases it.
        foreach (var (entity, pad) in context.Query<DemoGamepad>())
        {
            padHeld = VehiclesScene.Hold.None;
            foreach (var (button, key) in PadKeys)
            {
                uint bit = 1u << (int)button;
                if ((pad.Buttons & bit) == 0)
                    continue;
                padHeld |= VehiclesScene.HoldOf(key);
                if ((lastButtons & bit) == 0)
                    OnKey(in context, key);
            }
            lastButtons = pad.Buttons;
        }

        foreach (var (entity, driver) in context.Query<VehicleDriver>())
            driver.Held = (int)(held | padHeld);
    }

    private static void OnKey(in SystemContext context, int key)
    {
        foreach (var (car, driverRef, vehicleRef) in context.Query<VehicleDriver, Vehicle>())
        {
            ref VehicleDriver driver = ref driverRef.Value;
            switch (key)
            {
            case VehiclesScene.KeyC:
                foreach (var (rig, camera) in context.Query<DemoCamera>())
                {
                    camera.Active = camera.Active == 0 ? 1 : 0;
                    context.Log.Info(camera.Active != 0 ? "C: the camera chases the car"
                                                        : "C: camera free: WASD flies it, the arrows drive");
                }
                break;
            case VehiclesScene.KeyL:
                LogState(in context, car, in driver);
                break;
            case VehiclesScene.KeyP:
                driver.Autopilot = !driver.Autopilot;
                driver.Parking = !driver.Autopilot;
                driver.Waypoint = 0;
                context.Log.Info(driver.Autopilot ? "P: autopilot: driving the track"
                                                  : "P: autopilot off: the car brakes to a stop until you drive");
                break;
            case VehiclesScene.KeyT:
                VehiclesScene.Respawn(car, driver.Trailer);
                driver.Waypoint = 0;
                context.Log.Info("T: the car and its trailer are back at the start, its engine and wheels still turning");
                break;
            case VehiclesScene.KeyR:
                driver.Right = true;
                break;
            case VehiclesScene.KeyK:
                driver.Kick = true;
                break;
            case VehiclesScene.KeyM:
                SwapGearbox(in context, ref driver, vehicleRef);
                break;
            case VehiclesScene.KeyE:
            case VehiclesScene.KeyQ:
                if (driver.Manual)
                {
                    int top = vehicleRef.Kind.Wheeled.Transmission.GearCount;
                    vehicleRef.Input.Gear = Math.Clamp(vehicleRef.Input.Gear + (key == VehiclesScene.KeyE ? 1 : -1), -1, top);
                    context.Log.Info($"{(key == VehiclesScene.KeyE ? "E" : "Q")}: gear {vehicleRef.Input.Gear}");
                }
                break;
            case VehiclesScene.KeyV:
                driver.DrawVehicles = !driver.DrawVehicles;
                PhysicsWorld.SetDebugDraw(driver.DrawVehicles ? PhysicsDebugCategories.Vehicles : PhysicsDebugCategories.None);
                context.Log.Info(driver.DrawVehicles ? "V: the vehicles' debug lines are on" : "V: debug lines off");
                break;
            case VehiclesScene.KeyU:
                SwapTrailerKind(in context, ref driver);
                break;
            }
        }
    }

    // M: the gearbox between Automatic and Manual, edited in place through the wheeled view, on in the gear it is in
    // or first from neutral.
    private static void SwapGearbox(in SystemContext context, ref VehicleDriver driver, Ref<Vehicle> vehicle)
    {
        driver.Manual = !driver.Manual;
        vehicle.Kind.Wheeled.Transmission.Mode = driver.Manual ? TransmissionMode.Manual : TransmissionMode.Automatic;
        vehicle.Input.Gear = vehicle.State.Gear == 0 ? 1 : vehicle.State.Gear;
        context.Log.Info(driver.Manual ? "M: Manual gearbox: E/Q shift, Shift holds the clutch" : "M: Automatic gearbox");
    }

    // U: the trailer given a new kind: a wheeled car with its default engine, which the driver gives the car's
    // Forward so it pushes, or a trailer again.
    private static void SwapTrailerKind(in SystemContext context, ref VehicleDriver driver)
    {
        Ref<Vehicle> trailer = driver.Trailer.Get<Vehicle>();
        driver.TrailerDriven = trailer.Kind.Type == VehicleType.Trailer;
        if (driver.TrailerDriven)
        {
            trailer.Kind = new WheeledVehicle();
        }
        else
        {
            trailer.Kind = new TrailerVehicle();
            trailer.Input.Forward = 0;
        }
        context.Log.Info(driver.TrailerDriven ? "U: the trailer is a wheeled car now, and pushes"
                                              : "U: the trailer is a trailer again");
    }

    // L: the car's whole state and each wheel's, as the C++ scene logs it.
    private static void LogState(in SystemContext context, Entity car, in VehicleDriver driver)
    {
        ref readonly VehicleState s = ref car.Get<Vehicle>().State;
        context.Log.Info($"L (tick {driver.Tick}): car {s.ForwardSpeed:0.00} m/s, {s.EngineRpm:0} RPM, gear {s.Gear}, " +
                         $"clutch {s.Clutch:0.00}, shifting {s.Shifting}, {s.WheelsOnGround} wheels on ground");
        Ref<Vehicle> trailer = driver.Trailer.Get<Vehicle>();
        context.Log.Info($"  trailer {trailer.State.ForwardSpeed:0.00} m/s, {trailer.State.WheelsOnGround} wheels on " +
                         $"ground, brake {trailer.Input.Brake:0.00}, handbrake {trailer.Input.Handbrake:0.00}");
        Entity[] wheels = [driver.Wheel0, driver.Wheel1, driver.Wheel2, driver.Wheel3];
        for (int i = 0; i < wheels.Length; ++i)
        {
            ref readonly VehicleWheelState w = ref wheels[i].Get<VehicleWheel>().State;
            context.Log.Info($"  wheel {i}: on ground {w.OnGround}, body {(w.OnGround ? VehiclesScene.NameOf(w.GroundBody) : "none")}, " +
                             $"collider {(w.OnGround ? VehiclesScene.NameOf(w.GroundCollider) : "none")}, contact " +
                             $"{VehiclesScene.Format(w.ContactPoint)}, normal {VehiclesScene.Format(w.ContactNormal)}, " +
                             $"ground velocity {VehiclesScene.Format(w.GroundVelocity)}, compression {w.Compression:0.000} m, " +
                             $"{w.SuspensionForce:0} N, slip {w.LongitudinalSlip:0.000} / {w.LateralSlip:0.000} rad, spin " +
                             $"{w.AngularVelocity:0.0} rad/s, steer {w.SteerAngle:0.000} rad");
        }
    }
}

// The car's game code, once each tick before physics steps: reads the state, then writes the driver input, the trailer's
// brakes, and R's and K's moves.
[System(Phase.Tick)]
public sealed class VehiclesDriver : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        float dt = context.Time.TickRate;
        bool following = true;
        foreach (var (rig, camera) in context.Query<DemoCamera>())
            following = camera.Active != 0;
        DemoGamepad pad = default;
        foreach (var (entity, gamepad) in context.Query<DemoGamepad>())
            pad = gamepad.Value;

        foreach (var (car, driverRef, vehicle) in context.Query<VehicleDriver, Vehicle>())
        {
            ref VehicleDriver driver = ref driverRef.Value;
            ++driver.Tick;
            Drive(in context, car, ref driver, vehicle, in pad, following);
            CopyToTrailer(in driver, vehicle);
            NoteUpturned(car, ref driver, vehicle, dt);
            if (driver.Right)
                RightCar(in context, car, in driver);
            if (driver.Kick)
                Kick(in context, car);
            driver.Right = false;
            driver.Kick = false;
        }
    }

    // The keys and the pad as normalised input, or the autopilot's while it drives. While the camera is free, WASD is
    // the camera's, so only the arrows drive. The pad wins beyond the deadzone.
    private static void Drive(in SystemContext context, Entity car, ref VehicleDriver driver, Ref<Vehicle> vehicle,
                              in DemoGamepad pad, bool following)
    {
        var held = (VehiclesScene.Hold)driver.Held;
        bool Down(VehiclesScene.Hold arrow, VehiclesScene.Hold letter) =>
            (held & arrow) != 0 || (following && (held & letter) != 0);
        float throttle = Down(VehiclesScene.Hold.Up, VehiclesScene.Hold.W) ? 1 : 0;
        float back = Down(VehiclesScene.Hold.Down, VehiclesScene.Hold.S) ? 1 : 0;
        float steer = (Down(VehiclesScene.Hold.Right, VehiclesScene.Hold.D) ? 1 : 0) -
                      (Down(VehiclesScene.Hold.Left, VehiclesScene.Hold.A) ? 1 : 0);
        float clutch = (held & VehiclesScene.Hold.Shift) != 0 ? 1 : 0;
        bool handbrake = (held & VehiclesScene.Hold.Space) != 0;
        // The left trigger is S's: it brakes, then reverses once stopped.
        throttle = pad.RightTrigger > VehiclesScene.Deadzone ? pad.RightTrigger : throttle;
        back = pad.LeftTrigger > VehiclesScene.Deadzone ? pad.LeftTrigger : back;
        steer = MathF.Abs(pad.LeftStick.X) > VehiclesScene.Deadzone ? pad.LeftStick.X : steer;
        if (driver.Autopilot)
            VehiclesAutopilot.Steer(in context, car, ref driver, vehicle, out throttle, out back, out steer);
        // Let go by the autopilot, it stops rather than rolls on, until the driver drives.
        driver.Parking = driver.Parking && throttle == 0 && back == 0 && steer == 0;

        vehicle.Input.Steer = steer;
        vehicle.Input.Handbrake = handbrake ? 1 : 0;
        vehicle.Input.Clutch = clutch;
        if (driver.Parking)
        {
            vehicle.Input.Forward = 0;
            vehicle.Input.Brake = 1;
            return;
        }
        if (driver.Manual)
        {
            // The gear sets the direction: forward is the pedal, and back brakes.
            vehicle.Input.Forward = throttle;
            vehicle.Input.Brake = back;
            return;
        }
        // Asked to go the other way, it brakes until it has all but stopped, then drives that way.
        float speed = vehicle.State.ForwardSpeed;
        float forward = throttle - back;
        float brake = 0;
        if (forward * driver.Direction < 0)
        {
            if (MathF.Abs(speed) > VehiclesScene.StoppedSpeed && speed * forward < 0)
            {
                brake = MathF.Abs(forward);
                forward = 0;
            }
            else
            {
                driver.Direction = forward > 0 ? 1 : -1;
            }
        }
        vehicle.Input.Forward = forward;
        vehicle.Input.Brake = brake;
    }

    // Trailer brakes are game code's: the car's brake and handbrake, copied each tick. Turned into a wheeled car (U),
    // it is given the car's Forward too, so it pushes.
    private static void CopyToTrailer(in VehicleDriver driver, Ref<Vehicle> car)
    {
        Ref<Vehicle> trailer = driver.Trailer.Get<Vehicle>();
        trailer.Input.Brake = car.Input.Brake;
        trailer.Input.Handbrake = car.Input.Handbrake;
        if (driver.TrailerDriven)
            trailer.Input.Forward = car.Input.Forward;
    }

    // The flip test: no wheel on the ground with the chassis's up more than 60° from world up, for a while.
    private static void NoteUpturned(Entity car, ref VehicleDriver driver, Ref<Vehicle> vehicle, float dt)
    {
        Vector3 up = VehiclesScene.Rotate(car.Get<Transform>().Value.Rotation, new Vector3(0, 1, 0));
        bool upturned = vehicle.State.WheelsOnGround == 0 && up.Y < MathF.Cos(60 * VehiclesScene.Degrees);
        driver.Upturned = upturned ? driver.Upturned + dt : 0;
    }

    // R, the recipe for a flipped vehicle: a Transform write upright, facing the way it faced, lifted 1 m, its
    // velocities zeroed; its trailer put back behind it.
    private static void RightCar(in SystemContext context, Entity car, in VehicleDriver driver)
    {
        Transform at = car.Get<Transform>().Value;
        Quaternion upright = VehiclesScene.Upright(at.Rotation);
        Vector3 position = at.Position + new Vector3(0, 1, 0);
        VehiclesScene.Place(car, position, upright);
        VehiclesScene.Place(driver.Trailer, position + VehiclesScene.Rotate(upright, VehiclesScene.TrailerOffset), upright);
        context.Log.Info($"R: the car is upright again at {VehiclesScene.Format(position)}");
    }

    // K: the car kicked over: thrown up and rolled about its forward axis, so R can be tried.
    private static void Kick(in SystemContext context, Entity car)
    {
        Vector3 ahead = VehiclesScene.Rotate(car.Get<Transform>().Value.Rotation, new Vector3(0, 0, -1));
        PhysicsWorld.AddImpulse(car, new Vector3(0, 6000, 0));
        PhysicsWorld.AddAngularImpulse(car, ahead * 3000);
        context.Log.Info("K: the car is kicked over");
    }
}

// P: drives a lap of the track by itself, a waypoint at a time, because a smoke run can only tap keys.
public static class VehiclesAutopilot
{
    public readonly record struct Waypoint(float X, float Z, float Speed = 10); // Speed in m/s.

    // North over the bumps and the ramp, round to the east and south down the grass, then back to the start.
    private static readonly Waypoint[] Route =
    [
        new(0, -15, 6),
        new(0, -32, 6),
        new(0, -42, 10),
        new(0, -70, 12),
        new(25, -82, 8),
        new(25, -60, 8),
        new(25, -10, 12),
        new(20, 15, 8),
        new(0, 15, 6),
        new(0, 0, 5),
    ];

    private const float Reach = 6; // m: it moves on this close to a waypoint.

    public static void Steer(in SystemContext context, Entity car, ref VehicleDriver driver, Ref<Vehicle> vehicle,
                             out float throttle, out float back, out float steer)
    {
        Waypoint next = Route[driver.Waypoint % Route.Length];
        Transform at = car.Get<Transform>().Value;
        var to = new Vector3(next.X - at.Position.X, 0, next.Z - at.Position.Z);
        throttle = 0;
        back = 0;
        steer = 0;
        if (to.Length < Reach)
        {
            ++driver.Waypoint;
            context.Log.Info($"Autopilot: waypoint {driver.Waypoint % Route.Length} reached");
            return;
        }
        Vector3 ahead = VehiclesScene.Rotate(at.Rotation, new Vector3(0, 0, -1));
        Vector3 right = VehiclesScene.Rotate(at.Rotation, new Vector3(1, 0, 0));
        float angle = MathF.Atan2(Vector3.Dot(to, right), Vector3.Dot(to, ahead));
        steer = Math.Clamp(2 * angle, -1, 1);
        // Slower through a sharp turn.
        float wanted = MathF.Abs(angle) > 0.5f ? 0.5f * next.Speed : next.Speed;
        throttle = vehicle.State.ForwardSpeed < wanted ? 0.7f : 0;
    }
}

// The chase camera, as game code writes one: behind and above the car, turning with it a little late, and writing
// where it is into the scene's DemoCamera. The pad's right stick turns it round the car, and it swings back behind on
// release. Freed (Active 0), it leaves the camera to the free-fly controls.
[System(Phase.Update, Order = 430)]
public sealed class VehiclesCamera : ISystem
{
    private const float Distance = 9;
    private const float Height = 1.5f;
    private const float Pitch = -15;
    private const float CatchUp = 3;          // How much of its turn it makes up per second.
    private const float OrbitYaw = 180;       // A full push sideways looks at the car from the front.
    private const float OrbitPitch = 20;
    private const float OrbitCatchUp = 6;     // As CatchUp, for following the stick and springing back.

    private float yaw = -90;
    private float orbitYaw;
    private float orbitPitch;

    public void OnUpdate(in SystemContext context)
    {
        float dt = context.Time.DeltaTime;
        Vector2 look = default;
        foreach (var (entity, pad) in context.Query<DemoGamepad>())
        {
            look.X = MathF.Abs(pad.RightStick.X) > VehiclesScene.Deadzone ? pad.RightStick.X : 0;
            look.Y = MathF.Abs(pad.RightStick.Y) > VehiclesScene.Deadzone ? pad.RightStick.Y : 0;
        }
        foreach (var (car, transform, driver) in context.Query<Transform, VehicleDriver>())
        {
            foreach (var (rig, camera) in context.Query<DemoCamera>())
            {
                if (camera.Active == 0)
                    continue;
                Vector3 ahead = VehiclesScene.Rotate(transform.Rotation, new Vector3(0, 0, -1));
                ahead.Y = 0;
                // Facing straight up or down, it keeps the way it looked.
                if (ahead.Length > 0.1f)
                {
                    float turn = MathF.Atan2(ahead.Z, ahead.X) / VehiclesScene.Degrees - yaw;
                    turn -= 360 * MathF.Round(turn / 360);
                    yaw += turn * MathF.Min(1, CatchUp * dt);
                }
                // Pushed down, the stick looks down from higher up.
                orbitYaw += (look.X * OrbitYaw - orbitYaw) * MathF.Min(1, OrbitCatchUp * dt);
                orbitPitch += (-look.Y * OrbitPitch - orbitPitch) * MathF.Min(1, OrbitCatchUp * dt);
                float turned = (yaw + orbitYaw) * VehiclesScene.Degrees;
                float pitch = (Pitch + orbitPitch) * VehiclesScene.Degrees;
                var front = new Vector3(MathF.Cos(pitch) * MathF.Cos(turned), MathF.Sin(pitch),
                    MathF.Cos(pitch) * MathF.Sin(turned));
                camera.Position = transform.Position + new Vector3(0, Height, 0) - front * Distance;
                camera.Yaw = yaw + orbitYaw;
                camera.Pitch = Pitch + orbitPitch;
            }
        }
    }
}

// The looks moved onto what they show, and the readings in the car's, the trailer's and each wheel's names, every
// frame, in the C++ scenes' format.
[System(Phase.Update, Order = 440)]
public sealed class VehiclesReadings : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        foreach (var (entity, transform, look) in context.Query<Transform, VehicleLook>())
        {
            Transform target = look.Target.Get<Transform>().Value;
            transform.Position = target.Position + VehiclesScene.Rotate(target.Rotation, look.Offset);
            transform.Rotation = target.Rotation;
        }

        foreach (var (car, driver, vehicle) in context.Query<VehicleDriver, Vehicle>())
        {
            ref readonly VehicleState state = ref vehicle.State;
            car.Get<Name>().Value.SetText(
                $"Car: {state.ForwardSpeed * 3.6f:0} km/h, {state.EngineRpm:0} RPM, gear {state.Gear}" +
                $"{(state.Shifting ? ", shifting" : "")}, clutch {state.Clutch:0.00}, {state.WheelsOnGround} on ground" +
                $"{(driver.Upturned >= 2 ? ", flipped" : "")}");
            // A trailer has no engine or gearbox to show.
            ref readonly VehicleState trailer = ref driver.Trailer.Get<Vehicle>().State;
            driver.Trailer.Get<Name>().Value.SetText(
                $"Trailer: {trailer.ForwardSpeed * 3.6f:0} km/h, {trailer.WheelsOnGround} on ground");
            Entity[] wheels = [driver.Wheel0, driver.Wheel1, driver.Wheel2, driver.Wheel3];
            for (int i = 0; i < wheels.Length; ++i)
                wheels[i].Get<Name>().Value.SetText(Reading(i, in wheels[i].Get<VehicleWheel>().Value));
        }
    }

    private static string Reading(int index, in VehicleWheel wheel)
    {
        ref readonly VehicleWheelState state = ref wheel.State;
        if (!state.OnGround)
            return $"Wheel {index}: in air";
        // Past the tyre's SlideSlip either way, it has lost its grip.
        bool sliding = MathF.Abs(state.LongitudinalSlip) > wheel.Longitudinal.SlideSlip ||
                       MathF.Abs(state.LateralSlip) > wheel.Lateral.SlideSlip;
        return $"Wheel {index}: {VehiclesScene.NameOf(state.GroundCollider)}, compression {state.Compression:0.000} m, " +
               $"slip {state.LongitudinalSlip:0.00} / {state.LateralSlip / VehiclesScene.Degrees:0.0}°" +
               $"{(sliding ? ", sliding" : "")}";
    }
}
