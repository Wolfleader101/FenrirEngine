# 15: Vehicles in C#

**What to build:** C# systems build, drive and read vehicles exactly as C++ does: every vehicle component a writable mirror, driving by field writes through `Ref<Vehicle>`, both states read in place, with no native function (spec: "FenrirPhysicsScripting and Fenrir.Physics", "Prerequisite: the `[TaggedUnion]` generator"; ADR-0015, ADR-0009).

- **`Fenrir.Physics` gains `Vehicles.cs`:** `[NativeComponent("Fenrir.Physics.…")]` structs `Vehicle`, `VehicleWheel`, `VehicleDifferential` and `VehicleAntiRollBar`, writable, with C++'s fields in order, PascalCase, and C++'s defaults from `new` (`Vehicle.Kind = new()`, `Probe = Cylinder`, `MaxTilt = π`, `Enabled = true`; every `EntityLink` `EntityLink.None`); `public readonly VehicleState State` and `public readonly VehicleWheelState State`, read through `Ref<T>`'s `ref readonly` accessor, writing them a compile error.
- **`VehicleKind` made with the `[TaggedUnion]` generator:** `[TaggedUnion(Noun = "vehicle")]`, tag `VehicleType : byte { Wheeled, Motorcycle, Tracked, Trailer }`, kinds `WheeledVehicle`, `MotorcycleVehicle`, `TrackedVehicle` and `TrailerVehicle` (one unused byte) at offset 4, views `Wheeled`, `Motorcycle`, `Tracked`, `Trailer`; `new VehicleKind()` is wheeled. The author writes the enum, the attribute, the tag and the four private fields at `[FieldOffset(4)]`, named `_wheeled`, `_motorcycle`, `_tracked` and `_trailer` by the generator's field-name rule; nothing else.
- **Settings and state types** under C++'s names: `VehicleEngine`, `VehicleTransmission` with `TransmissionMode`, `VehicleTrack`, `MotorcycleLean`, `VehicleSuspension` (whose `Spring` is the existing `JointSpring`), `VehicleTyre`, `VehicleInput`, `VehicleProbe`, `VehicleTrackSide`; `VehicleState` and `VehicleWheelState` as `readonly struct`s with `readonly` fields, `GroundBody` and `GroundCollider` `EntityLink`s.
- **`DebugDraw.Vehicles`** in `PhysicsWorld.cs`, the C++ bit.
- **No native functions and no sugar:** no `PhysicsNative` entries, no `IsGrounded`, km/h or `VehicleInput.FromSteer`. Bools inside kinds and state are read in place.
- **Doc comments** carry the short recipes: on `Vehicle`, getting in and out, flipped vehicles and towing; on `VehicleWheel`, wheel entities unscaled and out of the chassis's hierarchy and the chassis unscaled; on `VehicleWheel.State`, one collision step of lag and skidding as the slips against `PeakSlip` and `SlideSlip`; on `VehicleInput`, persistence, gear and clutch numbering, Manual's pedal; on `VehicleSuspension.Spring`, writing the damping too. `default(...)` behaviour per the spec, and "make each with `new`".
- **`FenrirPhysicsScripting`:** the four component mappings; a plain stand-in mirror of `VehicleKind`'s private union, hand-written as for the other two (the generator emits no C++); an `EntityLinkView` for `vehicle`, `leftWheel`, `rightWheel`, `groundBody` and `groundCollider`. FenrirPhysics still names nothing from Iris.

**Blocked by:** 02 (Engine and gearbox); 03 (Wheel settings, probe and tilt limit); 04 (Differentials and anti-roll bars); 07 (Motorcycles); 08 (Tracked vehicles); 09 (Trailers and towing); 13 (Vehicles debug lines); the `[TaggedUnion]` generator, built: `.scratch/fenrir-scripting-tagged-union/issues/02-tagged-union-tracer-bullet.md` and `03-tagged-union-diagnostics.md`.

**Status:** ready-for-agent

- [ ] `PhysicsScriptingTests` with a `Vehicles.cs` fixture: the four components validate against their native layouts; each `new` equals C++'s `{}`, including `new VehicleKind()`, the empty links and `MaxTilt = π`.
- [ ] A C# system builds a chassis and four wheel entities linked in that same system; after playback `Input.Forward = 1` raises `State.ForwardSpeed`; `VehicleWheel.State.OnGround` is true and `GroundBody` names the floor.
- [ ] `Kind.Wheeled.Transmission.Mode = Manual` then `Input.Gear = 2` shows in `State.Gear`; assigning a new kind (wheeled to trailer) reaches physics; the wrong view throws `InvalidOperationException`.
- [ ] A `VehicleDifferential` built from C# drives only its wheels; `DebugDraw.Vehicles` equals the C++ bit.
- [ ] Compile-fail cases: writing `Vehicle.State` or `VehicleWheel.State`, or a field of either, through `Ref<T>` does not compile.
- [ ] Disabled and inactive vehicles read reset `State`; nothing throws.
