using System.Runtime.InteropServices;
using Fenrir.Scripting;

namespace Fenrir.Physics;

// The C# mirrors of FenrirPhysics's vehicles (FenrirPhysics/Vehicles.hpp, ADR-0015), checked against the native layout
// whenever a script assembly loads, as the components in Components.cs are. The C++ documentation is the reference for
// what each field means and how out-of-range values are corrected; these comments carry what game code needs. Each has
// the C++ defaults when made with `new`: make every one with `new`. There are no vehicle native functions: driving is
// field writes through Ref<Vehicle>, and both states are read in place. VehicleKind and the states never cross from C++
// to C# by value (in an event or a call's argument), so the bools inside them are safe.

/// <summary>
/// Driver input: what game code writes on a vehicle to drive it, normalised, through <c>Ref&lt;Vehicle&gt;</c> in a
/// <see cref="Phase.Tick"/> system. Physics applies it after the Tick systems, clamped silently to each field's range.
/// It persists until game code changes it and physics never writes it, so what game code wrote is what it reads. Any
/// change to it wakes the chassis.
/// </summary>
public struct VehicleInput
{
    /// <summary>
    /// [-1, 1]: how hard to drive, negative in reverse (Automatic). In Manual a pedal, [0, 1], <see cref="Gear"/>
    /// setting the direction; a negative value is taken as 0. Trailers ignore it.
    /// </summary>
    public float Forward;
    /// <summary>[-1, 1], + right. Tracked vehicles ignore it.</summary>
    public float Steer;
    /// <summary>[0, 1].</summary>
    public float Brake;
    /// <summary>[0, 1]. Tracked vehicles ignore it.</summary>
    public float Handbrake;
    /// <summary>Tracked only: the left track's speed ratio, [-1, 1]; 0 holds the track still.</summary>
    public float LeftTrack = 1;
    /// <summary>Tracked only: the right track's speed ratio, [-1, 1]; 0 holds the track still.</summary>
    public float RightTrack = 1;
    /// <summary>
    /// Manual only (Automatic ignores it): -1 reverse, 0 neutral, 1..n forward, clamped to the gearbox's gears.
    /// </summary>
    public int Gear;
    /// <summary>Manual only: the clutch pedal, [0, 1], 0 engaged.</summary>
    public float Clutch;

    public VehicleInput()
    {
    }
}

/// <summary>How every wheel of a vehicle finds the ground beneath it.</summary>
public enum VehicleProbe : byte
{
    /// <summary>A ray down the suspension, with a fixed 80° slope limit against the world's up.</summary>
    Ray,
    /// <summary>A sphere cast down the suspension, with the same slope limit.</summary>
    Sphere,
    /// <summary>A cylinder the wheel's size cast down the suspension, with no slope limit: wheels roll over kerbs.</summary>
    Cylinder,
}

/// <summary>A wheel's suspension: how far it can rise, and how hard it pushes back.</summary>
public struct VehicleSuspension
{
    /// <summary>Metres: the suspension travel, how far the wheel can rise from full extension. 0 is rigid.</summary>
    public float Travel = 0.2f;
    /// <summary>
    /// The spring, in hertz and a damping ratio; a frequency of 0 is rigid. A <c>new JointSpring()</c>'s damping is 1,
    /// not the suspension's 0.5, so <c>new JointSpring { Frequency = 2 }</c> gives damping 1: write both.
    /// </summary>
    public JointSpring Spring = new() { Frequency = 1.5f, Damping = 0.5f };
    /// <summary>Metres the spring is compressed at full extension, so it pushes from the start.</summary>
    public float Preload;

    public VehicleSuspension()
    {
    }
}

/// <summary>
/// A tyre's friction against its slip: it rises from 0 at no slip to <see cref="PeakFriction"/> at
/// <see cref="PeakSlip"/>, falls to <see cref="SlideFriction"/> at <see cref="SlideSlip"/>, and stays there. Along the
/// wheel slip is a ratio, across it an angle in radians. It combines with the ground's
/// <see cref="PhysicsMaterial.Friction"/> as the geometric mean, as bodies combine theirs.
/// </summary>
public struct VehicleTyre
{
    public float PeakSlip;
    public float PeakFriction;
    public float SlideSlip;
    public float SlideFriction;
}

/// <summary>A vehicle's engine. Its torque curve is built from the shape fields; the engine never stalls.</summary>
public struct VehicleEngine
{
    /// <summary>Newton metres, at <see cref="PeakTorqueRpm"/>.</summary>
    public float MaxTorque = 500;
    /// <summary>The idle speed, in RPM.</summary>
    public float MinRpm = 1000;
    public float MaxRpm = 6000;
    public float PeakTorqueRpm = 4000;
    /// <summary>The fraction of <see cref="MaxTorque"/> at <see cref="MinRpm"/>.</summary>
    public float TorqueAtMinRpm = 0.8f;
    /// <summary>The fraction of <see cref="MaxTorque"/> at <see cref="MaxRpm"/>.</summary>
    public float TorqueAtMaxRpm = 0.8f;
    /// <summary>kg m².</summary>
    public float Inertia = 0.5f;
    public float AngularDamping = 0.2f;

    public VehicleEngine()
    {
    }
}

/// <summary>
/// Who changes gear: the gearbox itself, or game code through <see cref="VehicleInput.Gear"/> and
/// <see cref="VehicleInput.Clutch"/>.
/// </summary>
public enum TransmissionMode : byte
{
    Automatic,
    Manual,
}

/// <summary>
/// A vehicle's gearbox: <see cref="GearCount"/> forward gears spaced geometrically from <see cref="FirstGear"/> to
/// <see cref="TopGear"/>, and one reverse gear. The shift and switch fields are read only in Automatic.
/// </summary>
public struct VehicleTransmission
{
    public TransmissionMode Mode;
    public int GearCount = 5;
    public float FirstGear = 2.66f;
    public float TopGear = 0.74f;
    /// <summary>The reverse gear's ratio, as a positive magnitude.</summary>
    public float ReverseGear = 2.90f;
    /// <summary>RPM.</summary>
    public float ShiftUpRpm = 4000;
    /// <summary>RPM.</summary>
    public float ShiftDownRpm = 2000;
    /// <summary>Seconds a shift takes.</summary>
    public float SwitchTime = 0.5f;
    /// <summary>Seconds the clutch takes to engage after a shift.</summary>
    public float ClutchReleaseTime = 0.3f;
    /// <summary>Seconds after a shift before the next.</summary>
    public float SwitchLatency = 0.5f;
    public float ClutchStrength = 10;

    public VehicleTransmission()
    {
    }
}

/// <summary>
/// A wheeled vehicle (a car, a truck): steered by turning wheels, driving every wheel equally through its gearbox until
/// game code names differentials.
/// </summary>
public struct WheeledVehicle
{
    public VehicleEngine Engine = new();
    public VehicleTransmission Transmission = new();
    /// <summary>The limited-slip ratio between differentials.</summary>
    public float LimitedSlipRatio = 1.4f;

    public WheeledVehicle()
    {
    }
}

/// <summary>How a motorcycle keeps itself up: it leans into turns, and stands at rest.</summary>
public struct MotorcycleLean
{
    /// <summary>Disabled, the bike falls over. A live edit.</summary>
    public bool Enabled = true;
    /// <summary>
    /// Radians: the most it leans, kept within 0 and a quarter turn. Changing it rebuilds the vehicle privately, keeping
    /// the engine speed, the gear and the wheels' spin.
    /// </summary>
    public float MaxAngle = MathF.PI / 4;
    /// <summary>Caps steering by speed so the lean stays under <see cref="MaxAngle"/>. A live edit.</summary>
    public bool LimitSteering = true;

    public MotorcycleLean()
    {
    }
}

/// <summary>
/// A motorcycle: two wheels in line, leaning into turns and standing at rest by itself through <see cref="Lean"/>. It
/// drives both wheels until game code names differentials: one <see cref="VehicleDifferential"/> naming only its rear
/// wheel rides like the physics backend's sample bike.
/// </summary>
public struct MotorcycleVehicle
{
    public VehicleEngine Engine = new() { MaxTorque = 150, MinRpm = 1000, MaxRpm = 10000, PeakTorqueRpm = 6000 };
    public VehicleTransmission Transmission = new()
    {
        GearCount = 6,
        FirstGear = 2.27f,
        TopGear = 0.88f,
        ReverseGear = 4,
        ShiftUpRpm = 8000,
        ShiftDownRpm = 2000,
        ClutchStrength = 2,
    };
    /// <summary>The limited-slip ratio between differentials.</summary>
    public float LimitedSlipRatio = 1.4f;
    public MotorcycleLean Lean = new();

    public MotorcycleVehicle()
    {
    }
}

/// <summary>
/// One track of a tracked vehicle: the wheels whose <see cref="VehicleWheel.Track"/> names its side, turning together.
/// Every field is a live edit. Its frictions combine with the ground's as a tyre's do.
/// </summary>
public struct VehicleTrack
{
    /// <summary>kg m²: the track's and its wheels' inertia, as seen on its driven wheel. Kept above 0.</summary>
    public float Inertia = 10;
    public float AngularDamping = 0.5f;
    /// <summary>Newton metres at full brake.</summary>
    public float MaxBrakeTorque = 15000;
    /// <summary>The gearbox's turns per turn of the track's driven wheel. Kept above 0.</summary>
    public float DifferentialRatio = 6;
    public float LongitudinalFriction = 4;
    public float LateralFriction = 2;

    public VehicleTrack()
    {
    }
}

/// <summary>
/// A tracked vehicle (a tank): two tracks, each driven at its own speed ratio, so it steers by
/// <see cref="VehicleInput.LeftTrack"/> and <see cref="VehicleInput.RightTrack"/> and pivots on the spot when they are
/// opposite. Turning steering into track ratios is game code's. It ignores <see cref="VehicleInput.Steer"/>,
/// <see cref="VehicleInput.Handbrake"/> and differentials, and its wheels' tyre, brake, steer and inertia fields. Each
/// track's driven wheel is its rearmost (the largest chassis-local Z).
/// </summary>
public struct TrackedVehicle
{
    public VehicleEngine Engine = new() { MinRpm = 500, MaxRpm = 4000, PeakTorqueRpm = 3000 };
    public VehicleTransmission Transmission = new()
    {
        GearCount = 4,
        FirstGear = 4,
        TopGear = 1,
        ReverseGear = 4,
        ShiftUpRpm = 3500,
        ShiftDownRpm = 1000,
    };
    public VehicleTrack Left = new();
    public VehicleTrack Right = new();

    public TrackedVehicle()
    {
    }
}

// The engine never reads this byte; it stands for the one byte an empty C++ struct takes.
#pragma warning disable CS0169

/// <summary>
/// A trailer, wagon or hand cart: a vehicle with no engine and no gearbox, towed by an ordinary <see cref="Joint"/>. It
/// reads <see cref="VehicleInput.Brake"/>, <see cref="VehicleInput.Handbrake"/> and <see cref="VehicleInput.Steer"/>
/// (for a steered dolly) and ignores the rest of the input and every differential. Its state's engine and gear fields
/// stay 0.
/// </summary>
public struct TrailerVehicle
{
    private byte _unused;
}

#pragma warning restore CS0169

/// <summary>Which kind of vehicle a <see cref="VehicleKind"/> holds.</summary>
public enum VehicleType : byte
{
    Wheeled,
    Motorcycle,
    Tracked,
    Trailer,
}

/// <summary>
/// A vehicle kind: exactly one kind of vehicle and its drivetrain, by value, laid out as the native
/// <c>Fenrir::VehicleKind</c> (a one-byte tag, then the kinds overlapping at the union's offset, 4 because every kind
/// holds at most 4-byte values). Assigning a kind changes it (<c>vehicle.Kind = new TrailerVehicle()</c>): a rebuild
/// that keeps the engine speed, the gear and the wheels' spin. Each kind's view reads or edits it in place
/// (<c>vehicle.Kind.Wheeled.Transmission.Mode = TransmissionMode.Manual</c>) and throws
/// <see cref="InvalidOperationException"/> if the vehicle is another kind. New, it is a wheeled vehicle.
/// </summary>
[TaggedUnion(Noun = "vehicle")]
[StructLayout(LayoutKind.Explicit)]
public partial struct VehicleKind
{
    [FieldOffset(0)] private VehicleType _type;
    [FieldOffset(4)] private WheeledVehicle _wheeled;
    [FieldOffset(4)] private MotorcycleVehicle _motorcycle;
    [FieldOffset(4)] private TrackedVehicle _tracked;
    [FieldOffset(4)] private TrailerVehicle _trailer;
}

/// <summary>
/// How the last collision step left a vehicle: written by physics into <see cref="Vehicle.State"/> after every step.
/// C# reads it in place and cannot write it. Fields a kind does not have stay 0. A sleeping vehicle keeps its last state.
/// </summary>
public readonly struct VehicleState
{
    /// <summary>Metres per second along the chassis's -Z (forward), signed.</summary>
    public readonly float ForwardSpeed;
    public readonly float EngineRpm;
    /// <summary>
    /// The engaged gear, numbered as <see cref="VehicleInput.Gear"/>: -1 reverse, 0 neutral, 1..n forward. In Automatic
    /// the gearbox's own choice: it drops to neutral braking to a stop.
    /// </summary>
    public readonly int Gear;
    /// <summary>The applied clutch, [0, 1], 0 engaged.</summary>
    public readonly float Clutch;
    /// <summary>Changing gear now.</summary>
    public readonly bool Shifting;
    public readonly int WheelsOnGround;
    /// <summary>Motorcycle only: radians, + right.</summary>
    public readonly float LeanAngle;
    /// <summary>Tracked only: metres per second.</summary>
    public readonly float LeftTrackSpeed;
    /// <summary>Tracked only: metres per second.</summary>
    public readonly float RightTrackSpeed;

    public VehicleState()
    {
    }
}

/// <summary>
/// Makes the entity's dynamic <see cref="RigidBody"/> a vehicle (ADR-0015): the body is its chassis, the only part of
/// the vehicle that collides. Its wheels are <see cref="VehicleWheel"/> components on wheel entities of their own, each
/// naming this entity. Chassis-local +Y is up and -Z is forward. A <see cref="Phase.Tick"/> system drives it by writing
/// <see cref="Input"/> through <c>Ref&lt;Vehicle&gt;</c> and reads <see cref="State"/> through the same reference. With
/// every default, a wheeled vehicle drives every wheel through an automatic five-speed gearbox.
/// </summary>
/// <remarks>
/// <para>
/// <b>The chassis.</b> Its body needs a solid collider. Keep the chassis unscaled: give it an unscaled
/// <see cref="BoxShape"/> of the right size and a scaled look as a child. Without a dynamic body and a solid collider,
/// or beside a <see cref="CharacterBody"/>, it is inactive, with one warning each time it becomes so. Disabled or
/// inactive, its <see cref="State"/> and its wheels' read reset. Nothing throws.
/// </para>
/// <para>
/// <b>Getting in and out</b> is game code: disable the driver's <see cref="CharacterBody"/> and parent it to a seat
/// child. To get out, unparent it beside the door, clear it with <see cref="PhysicsWorld.Depenetrate"/> or
/// <see cref="PhysicsWorld.MoveCast"/>, then enable it again.
/// </para>
/// <para>
/// <b>Flipped vehicles:</b> one is flipped once <see cref="VehicleState.WheelsOnGround"/> has been 0 for a while with
/// the chassis's up turned well away from world up. Right it with a <see cref="Transform"/> write (upright, lifted, its
/// velocities zeroed) or a torque (<see cref="PhysicsWorld.AddTorque"/>), or set <see cref="MaxTilt"/> to about 60° so it never rolls over.
/// </para>
/// <para>
/// <b>Towing</b> is an ordinary <see cref="Joint"/>, usually a <see cref="BallJoint"/> at the hitch to a
/// <see cref="TrailerVehicle"/>. Trailer brakes are game code's: copy the tractor's <see cref="VehicleInput.Brake"/>
/// and <see cref="VehicleInput.Handbrake"/> into the trailer's <see cref="Input"/> each tick; nothing links them.
/// </para>
/// <para>
/// Make it with <c>new</c>. <c>default(Vehicle)</c> is all zero bytes: a disabled vehicle whose kind is a wheeled one
/// with zeroed settings, harmless and silent.
/// </para>
/// </remarks>
[NativeComponent("Fenrir.Physics.Vehicle")]
public struct Vehicle : IComponent
{
    /// <summary>The kind and its drivetrain: a <see cref="WheeledVehicle"/> when new.</summary>
    public VehicleKind Kind = new();
    public VehicleInput Input = new();
    /// <summary>How every wheel finds the ground. A live edit.</summary>
    public VehicleProbe Probe = VehicleProbe.Cylinder;
    /// <summary>
    /// Radians: the cone around world up the chassis cannot pitch or roll past, a live edit. π (when new) is off; about
    /// 60° stops rollovers. Kept within 0 to π.
    /// </summary>
    public float MaxTilt = MathF.PI;
    /// <summary>
    /// Disabled, the entity is a plain rigid body resting on its chassis and its state and its wheels' reset. Enabled
    /// again, it is built fresh with nothing carried over.
    /// </summary>
    public bool Enabled = true;
    /// <summary>Read-back, overwritten by physics after every step: read it through <c>Ref&lt;Vehicle&gt;</c>, never write it.</summary>
    public readonly VehicleState State = new();

    public Vehicle()
    {
    }
}

/// <summary>Which of a tracked vehicle's two tracks a wheel sits on.</summary>
public enum VehicleTrackSide : byte
{
    Left,
    Right,
}

/// <summary>
/// How the last collision step left a wheel: written by physics into <see cref="VehicleWheel.State"/> after every
/// step. C# reads it in place and cannot write it. Off the ground, the ground fields are empty or 0.
/// </summary>
/// <remarks>
/// Contact, compression and steer lag one collision step: they come from that step's probes against the chassis's pose
/// after it. A skid is game code's to judge: compare <see cref="LongitudinalSlip"/> and <see cref="LateralSlip"/> with
/// the wheel's tyres' <see cref="VehicleTyre.PeakSlip"/> (past the grip's peak) and <see cref="VehicleTyre.SlideSlip"/>
/// (sliding).
/// </remarks>
public readonly struct VehicleWheelState
{
    public readonly bool OnGround;
    /// <summary>The body entity of the ground, on ground only; <see cref="EntityLink.None"/> otherwise.</summary>
    public readonly EntityLink GroundBody = EntityLink.None;
    /// <summary>The collider entity of the ground touched, on ground only; <see cref="EntityLink.None"/> otherwise.</summary>
    public readonly EntityLink GroundCollider = EntityLink.None;
    /// <summary>World space.</summary>
    public readonly Vector3 ContactPoint;
    /// <summary>World space, unit length.</summary>
    public readonly Vector3 ContactNormal;
    /// <summary>Metres per second, the ground's velocity at the contact.</summary>
    public readonly Vector3 GroundVelocity;
    /// <summary>Metres risen from full extension: 0 to the suspension's travel.</summary>
    public readonly float Compression;
    /// <summary>Newtons.</summary>
    public readonly float SuspensionForce;
    /// <summary>Along the wheel, a ratio in <see cref="VehicleTyre"/>'s terms.</summary>
    public readonly float LongitudinalSlip;
    /// <summary>Across the wheel, radians.</summary>
    public readonly float LateralSlip;
    /// <summary>Radians per second, + rolls forward.</summary>
    public readonly float AngularVelocity;
    /// <summary>Radians, + right, as <see cref="VehicleInput.Steer"/>.</summary>
    public readonly float SteerAngle;

    public VehicleWheelState()
    {
    }
}

/// <summary>
/// One wheel of a vehicle, on its own wheel entity, naming the vehicle's chassis. It is never a body and nothing
/// collides with it. Wheel order is entity order. Link it by assigning the chassis (<c>wheel.Vehicle = chassis</c>),
/// including one made by <see cref="Entity.Create"/> earlier in the same system.
/// </summary>
/// <remarks>
/// <para>
/// Physics poses the wheel entity for drawing, from the chassis's pose and the wheel's own: its Transform is output
/// only, and physics never reads a mount point from it. Keep wheel entities unscaled and out of the chassis's
/// hierarchy (not its children), and give each a scaled look as a child; keep the chassis unscaled too.
/// </para>
/// <para>
/// Every setting is a live edit. A wheel naming no entity of its scene with a <see cref="Vehicle"/>, or sharing its
/// entity with a <see cref="RigidBody"/>, a <see cref="CharacterBody"/> or any <see cref="Collider"/>, is inactive, with
/// one warning each time it becomes so.
/// </para>
/// <para>
/// Make it with <c>new</c>. <c>default(VehicleWheel)</c> is all zero bytes: its link names entity 0 with no scene, so
/// it is an inactive wheel, with that one warning.
/// </para>
/// </remarks>
[NativeComponent("Fenrir.Physics.VehicleWheel")]
public struct VehicleWheel : IComponent
{
    /// <summary>The chassis entity: the entity with the <see cref="Fenrir.Physics.Vehicle"/>.</summary>
    public EntityLink Vehicle = EntityLink.None;
    /// <summary>Chassis-local metres from the chassis origin: the wheel's centre at full extension.</summary>
    public Vector3 Position;
    /// <summary>Tracked vehicles only.</summary>
    public VehicleTrackSide Track;
    /// <summary>Metres, never scaled.</summary>
    public float Radius = 0.3f;
    /// <summary>Metres, never scaled.</summary>
    public float Width = 0.1f;
    /// <summary>kg m².</summary>
    public float Inertia = 0.9f;
    public float AngularDamping = 0.2f;
    /// <summary>Radians: + tilts the top of the suspension and steering axis back (+Z), as a motorcycle's fork.</summary>
    public float Caster;
    /// <summary>Radians: + tilts the wheel's top outwards. Mirrored by the sign of <see cref="Position"/>'s X.</summary>
    public float Camber;
    /// <summary>Radians: + turns the wheel's front inwards (toe-in). Mirrored by the sign of <see cref="Position"/>'s X.</summary>
    public float Toe;
    public VehicleSuspension Suspension = new();
    /// <summary>Radians at full steer: 0 does not steer.</summary>
    public float MaxSteerAngle;
    /// <summary>Newton metres at full brake.</summary>
    public float MaxBrakeTorque = 1500;
    /// <summary>Newton metres at full handbrake.</summary>
    public float MaxHandbrakeTorque = 4000;
    public VehicleTyre Longitudinal = new() { PeakSlip = 0.06f, PeakFriction = 1.2f, SlideSlip = 0.2f, SlideFriction = 1 };
    public VehicleTyre Lateral = new() { PeakSlip = 0.052f, PeakFriction = 1.2f, SlideSlip = 0.35f, SlideFriction = 1 };
    /// <summary>
    /// Read-back, overwritten by physics after every step: read it through <c>Ref&lt;VehicleWheel&gt;</c>, never write
    /// it. It lags one collision step; a skid is its slips compared with the tyres' <see cref="VehicleTyre.PeakSlip"/>
    /// and <see cref="VehicleTyre.SlideSlip"/>.
    /// </summary>
    public readonly VehicleWheelState State = new();

    public VehicleWheel()
    {
    }
}

/// <summary>
/// A differential, on any entity: shares a vehicle's engine torque between two of its wheels, or gives one wheel its
/// share. A vehicle with no differential naming its wheels drives every wheel equally; adding any differential replaces
/// that, and a wheel in no differential only rolls and brakes. A rear-wheel-drive car is one differential naming its two
/// rear wheels.
/// </summary>
/// <remarks>
/// An empty wheel (<see cref="EntityLink.None"/>, as when new) is none. A wheel that is not a wheel entity, or wheels of
/// two different vehicles, make it inactive, with a warning. Make it with <c>new</c>: <c>default(VehicleDifferential)</c>
/// names entity 0 with no scene, so it is inactive, with that warning.
/// </remarks>
[NativeComponent("Fenrir.Physics.VehicleDifferential")]
public struct VehicleDifferential : IComponent
{
    /// <summary>A wheel entity, or <see cref="EntityLink.None"/> for none.</summary>
    public EntityLink LeftWheel = EntityLink.None;
    /// <summary>A wheel entity of the same vehicle, or <see cref="EntityLink.None"/> for none.</summary>
    public EntityLink RightWheel = EntityLink.None;
    /// <summary>The gearbox's turns per turn of the wheels.</summary>
    public float Ratio = 3.42f;
    /// <summary>How the torque is split across the two wheels: 0 all left, 0.5 even, 1 all right.</summary>
    public float LeftRightSplit = 0.5f;
    /// <summary>
    /// The fastest wheel's speed over the slowest's past which all the torque goes to the slower one: above 1. A very
    /// large value is an open differential.
    /// </summary>
    public float LimitedSlipRatio = 1.4f;
    /// <summary>This differential's share of the engine torque against its vehicle's others; the shares are normalised.</summary>
    public float TorqueShare = 1;

    public VehicleDifferential()
    {
    }
}

/// <summary>
/// An anti-roll bar, on any entity: stops a vehicle leaning in corners by pushing its two wheels' suspensions towards
/// the same compression, while both wheels are on the ground. A vehicle has none by default.
/// </summary>
/// <remarks>
/// A wheel that is not a wheel entity, or wheels of two different vehicles, make it inactive, with a warning; with a
/// wheel empty it does nothing. Make it with <c>new</c>: <c>default(VehicleAntiRollBar)</c> names entity 0 with no
/// scene, so it is inactive, with that warning.
/// </remarks>
[NativeComponent("Fenrir.Physics.VehicleAntiRollBar")]
public struct VehicleAntiRollBar : IComponent
{
    /// <summary>A wheel entity, usually on the vehicle's left.</summary>
    public EntityLink LeftWheel = EntityLink.None;
    /// <summary>A wheel entity of the same vehicle, usually the left wheel's partner on the right.</summary>
    public EntityLink RightWheel = EntityLink.None;
    /// <summary>Newtons per metre: the push per metre of difference in the two wheels' compression. 0 does nothing.</summary>
    public float Stiffness = 1000;

    public VehicleAntiRollBar()
    {
    }
}
