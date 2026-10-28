using Fenrir.Scripting;

namespace Fenrir.Physics;

// The hand-written parts of FenrirPhysics's vehicles (FenrirPhysics/Vehicles.hpp, ADR-0015). The fields, defaults and
// docs are in the generated mirrors (Generated/), checked against the native layout whenever a script assembly loads, as
// the components are; the C++ documentation is the reference for what each field means and how out-of-range values are
// corrected, and these remarks carry what game code in C# needs beside it. Each has the C++ defaults when made with
// `new`: make every one with `new`. There are no vehicle native functions: driving is field writes through
// Ref<Vehicle>, and both states are read in place. VehicleKind and the states never cross from C++ to C# by value (in an
// event or a call's argument), so the bools inside them are safe.

/// <remarks>From C#, write it through <c>Ref&lt;Vehicle&gt;</c> in a <see cref="Phase.Tick"/> system.</remarks>
public partial struct VehicleInput
{
}

/// <remarks>
/// From C#: a <c>new JointSpring()</c>'s damping is 1, not the suspension's 0.5, so <c>new JointSpring { Frequency = 2 }</c>
/// gives damping 1: write both.
/// </remarks>
public partial struct VehicleSuspension
{
}

/// <remarks>
/// From C#: laid out as the native <c>Fenrir::VehicleKind</c> (a one-byte tag, then the kinds overlapping at the union's
/// offset, 4 because every kind holds at most 4-byte values). Assigning a kind changes it
/// (<c>vehicle.Kind = new TrailerVehicle()</c>); each kind's view reads or edits it in place
/// (<c>vehicle.Kind.Wheeled.Transmission.Mode = TransmissionMode.Manual</c>) and throws
/// <see cref="InvalidOperationException"/> if the vehicle is another kind. New, it is a wheeled vehicle.
/// </remarks>
public partial struct VehicleKind
{
}

/// <remarks>From C#: read it through <c>Ref&lt;Vehicle&gt;</c>, in place; C# cannot write it.</remarks>
public readonly partial struct VehicleState
{
}

/// <remarks>
/// From C#: a <see cref="Phase.Tick"/> system drives it by writing <see cref="Input"/> through <c>Ref&lt;Vehicle&gt;</c>
/// and reads <see cref="State"/> through the same reference; C# cannot write <see cref="State"/>. Getting in and out is
/// game code: disable the driver's <see cref="CharacterBody"/> and parent it to a seat child; to get out, unparent it
/// beside the door, clear it with <see cref="PhysicsWorld.Depenetrate"/> or <see cref="PhysicsWorld.MoveCast"/>, then
/// enable it again. Right a flipped vehicle with a <see cref="Transform"/> write or a torque
/// (<see cref="PhysicsWorld.AddTorque"/>). Trailer brakes are game code's: copy the tractor's
/// <see cref="VehicleInput.Brake"/> and <see cref="VehicleInput.Handbrake"/> into the trailer's <see cref="Input"/> each
/// tick. Nothing throws. Make it with <c>new</c>. <c>default(Vehicle)</c> is all zero bytes: a disabled vehicle whose
/// kind is a wheeled one with zeroed settings, harmless and silent.
/// </remarks>
public partial struct Vehicle
{
}

/// <remarks>From C#: read it through <c>Ref&lt;VehicleWheel&gt;</c>, in place; C# cannot write it.</remarks>
public readonly partial struct VehicleWheelState
{
}

/// <remarks>
/// From C#: link it by assigning the chassis (<c>wheel.Vehicle = chassis</c>), including one made by
/// <see cref="Entity.Create"/> earlier in the same system. Make it with <c>new</c>. <c>default(VehicleWheel)</c> is all
/// zero bytes: its link names entity 0 with no scene, so it is an inactive wheel, with that one warning.
/// </remarks>
public partial struct VehicleWheel
{
}

/// <remarks>
/// From C#: an empty wheel is <see cref="Entity.None"/>, as when new. Make it with <c>new</c>:
/// <c>default(VehicleDifferential)</c> names entity 0 with no scene, so it is inactive, with a warning.
/// </remarks>
public partial struct VehicleDifferential
{
}

/// <remarks>
/// From C#: make it with <c>new</c>. <c>default(VehicleAntiRollBar)</c> names entity 0 with no scene, so it is inactive,
/// with a warning.
/// </remarks>
public partial struct VehicleAntiRollBar
{
}
