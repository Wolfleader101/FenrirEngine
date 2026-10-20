Status: ready-for-agent

# FenrirPhysics vehicles: cars, motorcycles, tracked vehicles and trailers on a full drivetrain, in C++ and C#

Origin: the FenrirPhysics wayfinder map (`.scratch/fenrir-physics/map.md`), whose vehicle tickets (40–48) this spec collapses into one buildable plan. ADR-0015 (vehicles are a component on their chassis, with one entity per wheel) records the hard-to-reverse call. ADR-0009 (engine packages extend scripting through extensions), ADR-0010 (the backend is hidden at compile time), ADR-0011 (physics components are plain data that physics diffs), ADR-0012 (each event reader sees each event once), ADR-0013 (joints live on joint entities) and ADR-0014 (character bodies are a velocity-driven component) still apply. A throwaway spike (`prototype/fenrir-physics-vehicles-spike`) proved the model through the seam with a car towing a trailer, a motorcycle and a tank driving a small track on F6. It builds on the foundation spec (`.scratch/fenrir-physics-foundation/spec.md`), the joints spec (`.scratch/fenrir-physics-joints/spec.md`) and the character spec (`.scratch/fenrir-physics-character/spec.md`), whose `DemoScenes` switcher, `DemoCamera`, writable entity links, read-only fields through `Ref<T>` and `PostTick` steps it extends.

## Problem Statement

FenrirPhysics simulates rigid bodies, joints and character bodies, but a game made with the engine cannot drive anything. A box on four sphere bodies jointed to it bounces, catches on every seam and has no engine, gearbox or brakes. Game code cannot make a car that accelerates through its gears, brakes, steers and grips tarmac better than ice; a motorcycle that leans into turns and stands at rest; a tank that pivots on the spot; or a trailer towed behind any of them. It cannot read a vehicle's speed, engine RPM and gear for a HUD, or each wheel's ground, compression and slip for skid marks and dust. It cannot draw wheels that spin, steer and ride their suspension.

Vehicles bring their own traps, which other engines show:
- wheels as an array inside the vehicle (Unreal, bevy_rapier, PhysX), which a blittable C# component cannot hold;
- physical per-wheel torques as the only input (Unity's `WheelCollider`, Godot, Rapier), leaving game code to write a drivetrain;
- defaults that do not drive (Godot's docs advise against its own), and tuning that is hard everywhere;
- a parked vehicle creeping or jittering on a slope, the most common complaint;
- ground grip that ignores the ground's material (every engine but PhysX);
- motorcycles that fall over (no engine surveyed keeps one up);
- a driver's own vehicle colliding with them, patched with ignore lists and collision groups;
- vehicles lost when their body is frozen and unfrozen (Godot).

The backend brings more: its vehicle runs inside the step on job threads, needs a ground probe the host sets, reads wheel settings by reference with no setters, does not wake its body on input, has a suspension bump stop that never engages under the obvious mapping, and has motorcycle lean damping that diverges on a light bike.

## Solution

FenrirPhysics gains **vehicles**. A vehicle is a `Vehicle` component on the entity of a dynamic `RigidBody`, its **chassis**, which is the only part of the vehicle that collides. Each wheel is a `VehicleWheel` component on its own **wheel entity**, naming the vehicle; physics poses the wheel entity's `Transform` for drawing. There is no vehicle entity apart from the chassis and no array anywhere.

Game code works the ECS way:
- **Making one:** add `Vehicle` beside a dynamic body's `RigidBody` and `Collider`, then one wheel entity per wheel with a `VehicleWheel` naming the chassis and giving a chassis-local `position`. Chassis-local +Y is up and −Z is forward. With every default, a wheeled vehicle drives on all its wheels with an automatic five-speed gearbox.
- **Kinds:** `Vehicle::kind` is a `VehicleKind` tagged union: wheeled (cars, trucks), motorcycle (leaning), tracked (two tracks, steered by their speeds) and trailer (no engine). Each alternative holds that kind's drivetrain. Changing kind is an assignment.
- **Driving:** write `Vehicle::input` in `Tick`: normalised `forward`, `steer`, `brake` and `handbrake`, plus `gear` and `clutch` for a manual gearbox and `leftTrack`/`rightTrack` for a tracked vehicle. Input persists until changed; physics never writes it.
- **The drivetrain is plain data:** the engine, gearbox, per-wheel suspension, brakes and tyres, motorcycle lean and tracks are fields with defaults that drive. Differentials and anti-roll bars are components of their own naming two wheel entities. Every setting is a live edit.
- **Read-back:** physics writes a `VehicleState` into `Vehicle` (speed, engine RPM, gear, clutch, wheels on ground, lean, track speeds) and a `VehicleWheelState` into each `VehicleWheel` (ground body and collider, contact, compression, suspension force, slips, spin, steer) after every step.
- **Ground grip** comes from the `PhysicsMaterial` of what each wheel touches, combined with the tyre's friction as bodies combine theirs.
- **Other bodies:** wheels push and load dynamic ground and ride moving ground through grip; the chassis is an ordinary dynamic body. Characters are never ground for a wheel. A trailer is any dynamic body (usually a `TrailerVehicle`) joined by an ordinary `Joint`.
- **Getting in and out, and righting a flipped vehicle,** are game code, with recipes in the docs and the C++ demo scene.

Everything is available from C# through the physics scripting extension, with no native functions: every vehicle component is a writable mirror, and both states are read-only through `ref readonly`. FenrirScripting gains the `[TaggedUnion]` generator, which writes `VehicleKind`'s C# members and takes over `CollisionShape` and `JointKind`.

A new debug-line category, `Vehicles`, draws each wheel's suspension, its circle, and its contact coloured by slip. The DemoApp gets two vehicle scenes on F6, one built in C++ and one built in C#, each with a chase camera written in its own language and demo-only gamepad driving.

## User Stories

### Using vehicles from C++

1. As a game developer, I want to make a vehicle by adding `Vehicle` beside a dynamic body's `RigidBody` and `Collider`, so that a vehicle is a component like any other.
2. As a game developer, I want one wheel entity per wheel with a `VehicleWheel` naming its vehicle, so that any number of wheels works, from a motorcycle's two to a tank's eighteen.
3. As a game developer, I want to give each wheel's `position` in chassis-local metres as its centre with the suspension fully extended, so that I place wheels from my model without guessing the sag.
4. As a game developer, I want chassis-local +Y up and −Z forward by convention, so that I never set axes and a model facing another way is turned on a child.
5. As a game developer, I want a vehicle with every default to drive on all its wheels with an automatic gearbox, so that I see it move before I tune anything.
6. As a game developer, I want a vehicle's kind (wheeled, motorcycle, tracked, trailer) to be one field I can assign, so that changing kind never adds or removes components.
7. As a game developer, I want to drive by writing normalised `forward`, `steer`, `brake` and `handbrake`, kept until I change them, so that keyboard, pad and AI all write the same four numbers once.
8. As a game developer, I want physics never to write my input, so that what I set is what I read.
9. As a game developer, I want a manual gearbox driven by `gear` and `clutch` in the input, so that I can build a stick-shift.
10. As a game developer, I want a tracked vehicle driven by each track's speed ratio, so that I decide how steering becomes track speeds and can pivot a tank.
11. As a game developer, I want the engine, gearbox, suspension, brakes and tyres as plain fields with SI defaults, so that tuning is field edits.
12. As a game developer, I want curves and gear ratios given as a few shape fields rather than arrays, so that every component stays plain data in both languages.
13. As a game developer, I want every setting to be a live edit applied on the next tick, so that tuning during play costs nothing.
14. As a game developer, I want each wheel to steer only when I give it a steer angle, so that front steering, rear steering and steered dollies are all one field.
15. As a game developer, I want caster, camber and toe per wheel, mirrored by side, so that a motorcycle's fork and a car's alignment are angles.
16. As a game developer, I want differentials as components naming two wheel entities, so that I choose front-, rear- or all-wheel drive and torque splits without arrays.
17. As a game developer, I want anti-roll bars as components naming two wheel entities, so that I can stop a car leaning in corners.
18. As a game developer, I want a motorcycle to stand at rest and lean into turns, with a switch to let it fall, so that bikes work without a balance controller of my own.
19. As a game developer, I want a tilt limit I can set, off by default, so that I can stop rollovers when my game wants that.
20. As a game developer, I want to choose how wheels find the ground (ray, sphere, cylinder), defaulting to a cylinder, so that wheels roll over kerbs.
21. As a game developer, I want a vehicle's speed, engine RPM, gear, clutch, shifting, wheels on ground, lean and track speeds written back every step, so that a HUD reads them as fields.
22. As a game developer, I want each wheel's ground body and collider, contact point and normal, compression, suspension force, slips, spin and steer written back every step, so that skid marks, dust and suspension sounds read fields.
23. As a game developer, I want the wheel entities posed by physics (spin, steer, suspension) and interpolated with the chassis, so that a mesh under each wheel entity is drawn right with no code.
24. As a game developer, I want tyre grip to come from the ground collider's `PhysicsMaterial`, combined as bodies combine friction, so that ice, grass and tarmac need no surface table.
25. As a game developer, I want a parked vehicle with its handbrake on a slope to stay put and fall asleep, so that parked cars never creep.
26. As a game developer, I want any input or vehicle edit to wake a sleeping vehicle, so that pressing the throttle always drives.
27. As a game developer, I want to switch a vehicle off with a field and have a plain rigid body resting on its chassis, so that a wreck is one field write.
28. As a game developer, I want a vehicle with no usable chassis, or wheels naming the wrong thing, to go inactive with one warning and come back once valid, so that mistakes are visible but never crash.
29. As a game developer, I want teleporting the chassis to keep engine RPM and wheel spin and snap the wheel poses, so that respawning a vehicle is a `Transform` write.
30. As a game developer, I want adding or removing a wheel, or changing kind, to keep engine RPM, gear and surviving wheels' spin, so that changes during play feel continuous.
31. As a game developer, I want wheels to push and load dynamic bodies they roll over and to ride kinematic platforms through grip, so that rubble moves and ferries carry vehicles.
32. As a game developer, I want wheels never to stand on a character, so that a car meets a pedestrian with its chassis rather than climbing them.
33. As a game developer, I want the chassis to be an ordinary body for contacts, queries, joints and triggers, so that a vehicle needs no special case elsewhere.
34. As a game developer, I want each wheel's ground probe to filter as the chassis's `CollisionFilter` does, and never to see triggers or its own chassis, so that layers alone decide what is ground.
35. As a game developer, I want a trailer kind with no engine that rolls, brakes and may steer, towed by an ordinary joint, so that trailers, wagons and hand carts need nothing new.
36. As a game developer, I want a recipe for getting a character in and out of a vehicle using fields I already have, so that I need no seat component and no collision groups.
37. As a game developer, I want a recipe for noticing and righting a flipped vehicle, so that I handle it my way.
38. As a game developer, I want a differential that loses one wheel to keep driving the other, with one warning, so that losing a wheel mid-drive is not a crash.
39. As a game developer, I want vehicles stepped the same way on every run, threaded or not, so that same-build determinism holds.

### Using vehicles from C#

40. As a script author, I want `Vehicle`, `VehicleWheel`, `VehicleDifferential` and `VehicleAntiRollBar` writable from C# with the C++ defaults, so that C# builds and drives vehicles exactly as C++ does.
41. As a script author, I want to drive through `Ref<Vehicle>` with `Input.Forward = 1`, so that driving costs no native call.
42. As a script author, I want to read `State` on the vehicle and each wheel through the same `Ref<T>` I write through, and a compile error if I write it, so that read-back is cheap and safe.
43. As a script author, I want `VehicleKind` to have the same views, conversions and wrong-kind check as `CollisionShape` and `JointKind`, so that every union reads the same.
44. As a script author, I want to link wheels, differentials and anti-roll bars to entities I made earlier in the same system, so that one system builds a whole vehicle.
45. As a script author, I want `DebugDraw.Vehicles` with the C++ bit, so that `SetDebugDraw` shows vehicle lines.
46. As a script author, I want doc comments on getting in and out, flipping, towing, slip and wheel looks, so that I avoid the traps without reading the C++ spec.

### Engine-wide

47. As an engine developer, I want a `[TaggedUnion]` generator that writes a union's C# members from its tag enum and fields, with `CollisionShape` and `JointKind` moved onto it, so that a fourth union, in the engine or a script, is a few lines.
48. As an engine developer, I want no Jolt type reachable from the vehicle interface, so that the backend stays replaceable.
49. As an engine developer, I want a `Vehicles` debug-line category, so that any renderer can show suspension, wheels and grip.
50. As an engine developer, I want `Rpm` spelled the same way in both languages, so that C++ and C# fields line up.

### DemoApp

51. As the engine developer, I want F6 to step Default → Vehicles (C++) → Vehicles (C#) → Default, so that vehicle scenes switch like the character scenes.
52. As the engine developer, I want the spike's track and vehicles rewritten against the final API and extended with getting in and out, a pedestrian, a ferry, flip and kick keys, rear-wheel drive with anti-roll bars, and tilt, wheel-destroy and kinematic keys, so that I can check every rule by hand.
53. As the engine developer, I want a C# scene built entirely by a script, with a differential, anti-roll bars, a braked trailer, a manual gearbox and a kind swap, so that I can see the C# surface working live.
54. As the engine developer, I want each scene's chase camera written in its own language, so that the C# scene is C# end to end.
55. As the engine developer, I want speed, RPM, gear and each wheel's ground, compression and slip live in entity names, so that I can watch them in the Scene Hierarchy.
56. As the engine developer, I want to drive with a gamepad as well as the keyboard, so that throttle and steering are analog when I check handling.
57. As the engine developer, I want an autopilot in each scene, so that smoke runs, which can only tap keys, drive a lap.

## Implementation Decisions

### Prerequisite: the `[TaggedUnion]` generator (FenrirScripting)

`VehicleKind` is the engine's third tagged union, the trigger "What does C# get for joints?" set for the generator ("What does C# get for vehicles?", decision 1). The generator is designed in its own ticket, **"What does a `[TaggedUnion]` generator emit, and what does its author still write?"** (`.scratch/fenrir-scripting-tagged-union/issues/01-tagged-union-generator.md`, resolved; build tickets 02–04 beside it). This spec does not redesign it. What the vehicles work needs from it, fixed by ticket 47 and amended by the generator ticket:

- **The author writes** the tag enum (any enum; the engine's are backed by `byte`), `[TaggedUnion(Noun = "vehicle")]` on a `[StructLayout(LayoutKind.Explicit)] public partial struct`, the tag field at `[FieldOffset(0)]`, and one private field per kind at one shared `[FieldOffset(n)]`, named `_` plus its enum member with the first letter lowered. The offset is written only there; the attribute has no `Offset`. The fields stay hand-written because the native mapping (`IRIS_FIELD_AS`) and Iris's layout check name them.
- **The generator emits** a parameterless constructor setting the tag-0 kind to its `new()` (so `new()` and `default` agree on the kind), `Type`, one `[UnscopedRef] ref` view per kind named after the enum member, an implicit conversion from each kind, and `InvalidOperationException` on the wrong view: "The {noun} is a {kind}, not a {wanted}.", the noun being `Noun` or, left out, the struct's name.
- **Build errors** (generator diagnostics FENRIR0030–0038) for, among others: a struct that is not partial; a field with no matching enum member, or an enum member with no field; kind fields not sharing one offset, or the tag not at 0; a kind that is not unmanaged; an author-written member the generator emits. The full list is the generator ticket's.
- **`CollisionShape` and `JointKind` move onto it** (generator build ticket 04; `CollisionShape`'s `_hull` becomes `_convexHull` in C# and its one native mapping string), with their existing tests passing unchanged, which proves the surface is the same. The FenrirScripting README's "Tagged unions" section is rewritten around the generator. Script authors can use it too.
- **`VehicleKind` needs nothing beyond that:** four kinds at offset 4, one of them empty (`TrailerVehicle`, one unused byte, as `FixedJoint`).
- The generator emits no C++. Each union's native side, including `VehicleKind`'s plain stand-in in `FenrirPhysicsScripting`, stays hand-written.
- No ADR: the generator emits exactly the hand-written surface, so pasting that code back removes it.
- **The `_hull` field name** is settled by the generator ticket: one name rule, no override, so the move renames it.
- It lands and is tested before the vehicle extension work.

**No other prerequisites.** Read-only fields through `Ref<T>` (the character spec) and `EntityLink` made from an `Entity` with `EntityLink.None` (the joints spec) are built. FenrirECS needs nothing new: the vehicle watches use the foundation's component watches.

### FenrirPhysics: the components (ADR-0015)

```cpp
struct VehicleInput // written by game code; physics never writes it
{
    float forward = 0.0f;    // [-1, 1]; negative reverses (Automatic). Manual: a pedal, [0, 1], the gear sets direction
    float steer = 0.0f;      // [-1, 1], + right; tracked vehicles ignore it
    float brake = 0.0f;      // [0, 1]
    float handbrake = 0.0f;  // [0, 1]; tracked vehicles ignore it
    float leftTrack = 1.0f;  // tracked only: track speed ratio, [-1, 1]; 0 holds the track still
    float rightTrack = 1.0f;
    int gear = 0;            // Manual only: -1 reverse, 0 neutral, 1..n forward; clamped
    float clutch = 0.0f;     // Manual only: the pedal, [0, 1], 0 engaged
};

enum class VehicleProbe : std::uint8_t { Ray, Sphere, Cylinder };

struct Vehicle // on the chassis: the entity of a dynamic RigidBody
{
    VehicleKind kind;                              // WheeledVehicle by default
    VehicleInput input;
    VehicleProbe probe = VehicleProbe::Cylinder;   // how every wheel finds the ground
    float maxTilt = std::numbers::pi_v<float>;     // rad: cone around world up the chassis can't pitch or roll past; π is off
    bool enabled = true;
    VehicleState state;                            // read-back
};

enum class VehicleTrackSide : std::uint8_t { Left, Right };

struct VehicleWheel // on its own wheel entity
{
    Entity vehicle;                            // the chassis entity
    Math::Vec3 position = Math::Vec3(0.0f);    // chassis-local, from the chassis origin, scaled with it: the centre at full extension
    VehicleTrackSide track = VehicleTrackSide::Left; // tracked vehicles only
    float radius = 0.3f;                       // m, never scaled
    float width = 0.1f;                        // m, never scaled
    float inertia = 0.9f;                      // kg m²
    float angularDamping = 0.2f;
    float caster = 0.0f;                       // rad, mirrored by the sign of position.x
    float camber = 0.0f;
    float toe = 0.0f;
    VehicleSuspension suspension;
    float maxSteerAngle = 0.0f;                // rad; 0 does not steer
    float maxBrakeTorque = 1500.0f;            // N m
    float maxHandbrakeTorque = 4000.0f;        // N m
    VehicleTyre longitudinal = {0.06f, 1.2f, 0.2f, 1.0f};
    VehicleTyre lateral = {0.052f, 1.2f, 0.35f, 1.0f};
    VehicleWheelState state;                   // read-back
};

struct VehicleDifferential // on any entity
{
    Entity leftWheel;                // an empty wheel is "none"
    Entity rightWheel;
    float ratio = 3.42f;
    float leftRightSplit = 0.5f;
    float limitedSlipRatio = 1.4f;
    float torqueShare = 1.0f;        // normalised privately across the vehicle
};

struct VehicleAntiRollBar // on any entity
{
    Entity leftWheel;
    Entity rightWheel;
    float stiffness = 1000.0f;       // N/m
};
```

- **The chassis is the body.** `Vehicle` sits beside the entity's own `RigidBody` and `Collider` (or a compound through children, as for any body). There is no separate vehicle entity, because a body is at most one vehicle. Rejected (ADR-0015): wheels as an array, a vehicle entity naming its body, one component type per kind, physical per-wheel input, and the wheel entity's `Transform` as its mount point.
- **Axes** are fixed by convention to chassis-local +Y up and −Z forward (the camera's default front), with no fields. Fields can be added later, defaulting to the convention.
- **Wheels:**
  - The `vehicle` link is explicit, never "my parent". **Wheel entities are kept out of the chassis's hierarchy** in the docs and demos (spike lesson 5): physics writes a wheel's world position and rotation but never its scale, so under a scaled chassis a wheel's mesh would stretch. This amends ADR-0015's "usually under the chassis" (see Further Notes).
  - `position` follows the chassis's world scale, as joint frames do; wheel dimensions are absolute metres. **A chassis must not be scaled to size its look** (spike lesson 5): use an unscaled chassis with a `BoxShape` of the right size and a scaled look as a child. The docs say so.
  - Wheel order is entity order, which is the backend's wheel index.
  - Adding, removing or relinking a wheel rebuilds each vehicle involved. There is no per-wheel `enabled`: destroying or unlinking the wheel entity takes the wheel off.
  - One wheel type serves every kind; fields a kind does not use are ignored. Tracked vehicles ignore the tyre, brake and steer fields.
  - **A wheel's look** is a child of its unscaled wheel entity, scaled to the wheel's size (in the demos, a sphere scaled to (width, 2 × radius, 2 × radius) with a stripe to show spin). The wheel entity holds only `VehicleWheel` and the `Transform` physics writes.
- **Units:** SI and radians, as for joints and characters (metres, newton-metres, kg m², N/m, Hz and damping ratio), except engine speeds (min, max, peak torque, shift points), which are in RPM. Tyre slip angles are radians.
- **`Rpm`** is spelled that way in both languages (`minRpm`, `engineRpm`; `MinRpm`, `EngineRpm`), amending ticket 43's `RPM`.
- **No arrays in any vehicle component.** Every list the backend takes (torque curve, tyre curves, gear ratios, differentials, anti-roll bars) is shape fields or a component of its own. A free-form curve can come later as a cooked handle like `CollisionMesh`.
- **Inactive** (component kept, no backend vehicle, one warning on each transition to invalid, back by itself once valid):
  - a vehicle without a dynamic `RigidBody` that has a usable collider, or beside a `CharacterBody`;
  - a wheel that names something that is not a vehicle, sits on the chassis entity, or shares its entity with a `RigidBody`, `CharacterBody` or any `Collider` (triggers included);
  - a differential or anti-roll bar whose wheels are wrong rather than dead: a non-wheel entity, or wheels of two different vehicles. Dead names are below, under edges.
  - A vehicle with no wheels is valid but inert.
- **Switching:** `enabled = false` leaves a plain rigid body resting on its chassis; its wheel entities stay where they last were and its `state` and its wheels' states reset. Parking is the handbrake's job.
- **No structural changes by physics:** physics only writes `state` fields and wheel `Transform`s; nothing it does adds or removes components.
- **Documented** on `VehicleSuspension::spring`: it is a `JointSpring`, whose own default damping is 1.0, so `.spring = {.frequency = 2.0f}` gives damping 1.0, not the suspension's 0.5; write both (spike lesson 4, kept as `JointSpring` by ticket 47).

### FenrirPhysics: settings and defaults

All defaults are the backend samples' starting points, checked by the spike to drive without changes.

- **Suspension:** `VehicleSuspension { float travel = 0.2f; JointSpring spring = {1.5f, 0.5f}; float preload = 0.0f; }`. `travel` is how far the wheel can rise from full extension (**suspension travel**). `travel` 0 or a spring frequency of 0 makes the suspension rigid. Stiffness mode is not offered.
- **Tyres:** `VehicleTyre { float peakSlip; float peakFriction; float slideSlip; float slideFriction; }`: friction rises from 0 to its peak, falls to the slide value, and stays there. Longitudinal slip is a ratio, lateral slip an angle in radians. The backend car sample's ×10 longitudinal cap is not carried over; the spike's default car does 0–100 km/h in 8.8 s without it.
- **Per-wheel angles,** all 0 by default (suspension straight down, steering about +Y, wheel facing −Z): `caster` tilts the suspension and steering axis back (a motorcycle's fork), `camber` tilts the wheel's top, `toe` turns its front. Each is signed to mean the same on both sides, mirrored by the sign of `position.x`; at x = 0 nothing is mirrored. Kingpin and sideways suspension tilt are left out, addable later.
- **Engine:** `VehicleEngine { maxTorque = 500 (N m); minRpm = 1000; maxRpm = 6000; peakTorqueRpm = 4000; torqueAtMinRpm = 0.8; torqueAtMaxRpm = 0.8; inertia = 0.5; angularDamping = 0.2; }`. The two fractions are of `maxTorque`; physics builds the backend's curve from them. The engine never stalls.
- **Gearbox:** `enum class TransmissionMode : std::uint8_t { Automatic, Manual };` and `VehicleTransmission { mode = Automatic; gearCount = 5; firstGear = 2.66; topGear = 0.74; reverseGear = 2.90; shiftUpRpm = 4000; shiftDownRpm = 2000; switchTime = 0.5; clutchReleaseTime = 0.3; switchLatency = 0.5; clutchStrength = 10; }`. Forward ratios are spaced geometrically from first to top; one reverse gear, given as a positive magnitude. The shift and switch fields are read only in Automatic. In Manual, `input.gear` and `input.clutch` apply at once and the clutch is game code's.
- **Kinds,** the `VehicleKind` tagged union (`enum class VehicleType : std::uint8_t { Wheeled, Motorcycle, Tracked, Trailer }`, default `WheeledVehicle`), in `CollisionShape`'s and `JointKind`'s pattern (`Type()`, `Is<T>()`, `As<T>()`):
  - `WheeledVehicle { VehicleEngine engine; VehicleTransmission transmission; float limitedSlipRatio = 1.4f; }`. The limited-slip ratio is the one *between* differentials.
  - `MotorcycleVehicle { engine = {maxTorque 150, 1000–10000 RPM, peak 6000}; transmission = {6 gears, 2.27 to 0.88, reverse 4, shift up 8000, down 2000, clutchStrength 2}; limitedSlipRatio = 1.4; MotorcycleLean lean; }`.
    - `MotorcycleLean { bool enabled = true; float maxAngle = π/4; bool limitSteering = true; }`. Disabled, the bike falls over. `limitSteering` caps steering by speed so the lean stays under `maxAngle`.
    - The doc comment says the backend marks motorcycles as still in development, and that a motorcycle drives both wheels unless given a rear-only `VehicleDifferential` (one wheel named), which is how it rides like the backend's sample (spike detail).
  - `TrackedVehicle { engine = {500–4000 RPM, peak 3000}; transmission = {4 gears, 4.0 to 1.0, reverse 4, shift up 3500, down 1000}; VehicleTrack left, right; }`.
    - `VehicleTrack { inertia = 10; angularDamping = 0.5; maxBrakeTorque = 15000; differentialRatio = 6; longitudinalFriction = 4; lateralFriction = 2; }`. Grip is per track, because one wheel type can't hold kind-dependent defaults.
  - `TrailerVehicle {}`: no engine and no gearbox. It reads `brake`, `handbrake` and `steer` (for a steered dolly) and ignores `forward`, gear and clutch. Hand carts and wagons use the same kind.
- **Drives out of the box:** a wheeled or motorcycle vehicle with no `VehicleDifferential` naming its wheels drives every wheel equally. Adding any differential replaces that default. A wheel in no differential only rolls and brakes. There are no anti-roll bars by default. Tracked and trailer vehicles ignore differentials; `VehicleAntiRollBar` works on any kind with wheels.
- **Differentials:** an empty wheel name is "none", so a rear-only drive is one differential with one wheel. Each vehicle's torque shares are normalised privately, so they never have to sum to 1.
- **Probe:** `Vehicle::probe` is live and **defaults to `Cylinder` for every kind** (spike lesson 3, amending ticket 43's `Ray` for tracked: one field has no kind-dependent default, and the tank drives and pivots on the cylinder). Ray and sphere use the world's −gravity as up with a fixed 80° slope limit; the cylinder has none.
- **Tilt:** `maxTilt` is the live cone around world up past which the chassis can't pitch or roll. π means off, the default. About 60° (the backend's samples) stops rollovers.
- **Live edits:** every settings field on every vehicle component is a live edit applied on the next tick. The only private rebuild among settings is changing `MotorcycleLean::maxAngle`.
- **Out-of-range values,** one warning per vehicle, wheel or component, as for joints:
  - negative torques, inertias, frictions, stiffnesses, travel and widths are treated as 0, and radius is raised to a small minimum;
  - `maxSteerAngle`, `caster`, `camber` and `toe` are clamped below π/2;
  - `minRpm < maxRpm` is enforced, `peakTorqueRpm` and the shift points are clamped into the range, and shift-down is kept below shift-up;
  - `gearCount ≥ 1`, and limited-slip ratios at or below 1 are raised to just above it.

  Driver input is clamped to its ranges silently, as joint position targets are. In Manual a negative `forward` is clamped to 0 (spike lesson 6).
- **Refused as tuning (ADR-0010):** probe frequency, the suspension force point, the cylinder's rounding, the tyre max-impulse callback, the motorcycle's lean spring, damping, integration, decay and smoothing gains, the per-vehicle gravity override (gravity is per world), draw sizes, the spring stiffness mode, and a centre-of-mass offset (spike lesson 8: none needed; a possible later addition beyond vehicles).

### FenrirPhysics: state

```cpp
struct VehicleState // written by physics at write-back; game code's edits are overwritten
{
    float forwardSpeed = 0.0f;   // m/s along chassis -Z, signed
    float engineRpm = 0.0f;
    int gear = 0;                // engaged gear: -1 reverse, 0 neutral, 1..n (input.gear's numbering)
    float clutch = 0.0f;         // applied clutch, [0, 1], 0 engaged
    bool shifting = false;
    int wheelsOnGround = 0;
    float leanAngle = 0.0f;      // motorcycle only; rad, + right
    float leftTrackSpeed = 0.0f; // tracked only; m/s
    float rightTrackSpeed = 0.0f;
};

struct VehicleWheelState // written by physics at write-back; game code's edits are overwritten
{
    bool onGround = false;
    Entity groundBody;                            // on ground only
    Entity groundCollider;                        // the touched sub-shape's collider entity
    Math::Vec3 contactPoint = Math::Vec3(0.0f);   // world space
    Math::Vec3 contactNormal = Math::Vec3(0.0f);  // world space, unit
    Math::Vec3 groundVelocity = Math::Vec3(0.0f); // the ground's velocity at the contact
    float compression = 0.0f;      // m risen from full extension, 0..suspension.travel
    float suspensionForce = 0.0f;  // N
    float longitudinalSlip = 0.0f; // ratio, in VehicleTyre's terms
    float lateralSlip = 0.0f;      // rad
    float angularVelocity = 0.0f;  // rad/s, + rolls forward
    float steerAngle = 0.0f;       // rad, + right (as input.steer)
};
```

- **Written back, not queried** (the characters' precedent): overwritten at every write-back (step 5); game code's edits are ignored and overwritten, and physics keeps no diff copy. Rejected: a `PhysicsWorld` query in the joints' style (a HUD and skid marks read every vehicle and wheel every tick, a native call each from C#; and per-wheel state can't live on `Vehicle`, which holds no array).
- **Timing:** the state describes the last collision step's probes against the chassis's post-step pose, so contact, compression and steer lag by one collision step, as the backend's own wheel pose does. The doc comments say so.
- In Automatic, `gear` and `clutch` are the gearbox's own choices (it drops to neutral braking to a stop). In Manual they echo the clamped input.
- Fields a kind doesn't have stay 0 (tracked wheels have no slip; a trailer's engine and gear fields). Ground fields are empty or zero off the ground. A ground entity destroyed before game code reads it is detected by its version.
- Left out: tyre forces, combined friction and a "skidding" bool. Game code compares the slips with `VehicleTyre`'s `peakSlip` and `slideSlip`.
- **Reset** to the defaults when disabled or inactive. A sleeping vehicle keeps its last state, with spin 0. A paused world keeps state.

### FenrirPhysics: the step in `PostTick`

The `PostTick` sequence, as the joints and character specs left it, gains vehicle work in steps 1, 2 and 5, and runs inside step 4. Steps 1, 2 and 5 are sequential, on the app's thread.

1. **Drain** the `Vehicle`, `VehicleWheel`, `VehicleDifferential` and `VehicleAntiRollBar` watches with the body, joint and character watches, by each entity's net effect. Backend vehicles are removed with the constraints, before their bodies are destroyed or rebuilt, and created after bodies (and joints), in chassis entity order. A chassis body rebuilt in one drain (a collider swapped, or its `RigidBody` removed and added) removes and recreates its vehicle, carrying over under the rebuild rule below. Wheel set changes mark each vehicle involved for a rebuild.
2. **Diff** vehicles after bodies (and characters), beside joint sync, in chassis entity order: validity (a chassis turned kinematic or static, a collider lost), `enabled`, kind changes and `MotorcycleLean::maxAngle` (private rebuilds), live settings, wheel edits and relinks, differential and anti-roll-bar edits, the chassis's world scale (wheel positions patched in place), then **driver input pushed**. Any change to `input` (including `gear` and `clutch`), any edit to a vehicle component, or a rebuild wakes the chassis.
3. Apply queued forces and impulses.
   - 3b, move characters, unchanged. Characters meet a vehicle's pre-step pose, as they do any body's.
4. **Step.** Each vehicle runs inside the step as the backend's step listener, once per collision step, on job threads, in parallel across vehicles, with every probe on every collision step.
5. Write back dynamic bodies, including each chassis; then each vehicle's `state`, its wheels' `state`, and **wheel poses**, parents before children.
   - 5b, break overloaded joints, unchanged.
6. Sort and send events, unchanged: vehicles add none.
7. Regenerate debug lines if enabled.

- **Determinism:** each vehicle gets a private constraint priority from its chassis entity order, as joints do. Each vehicle's step writes only its own vehicle, so the parallel order doesn't matter. The spike's car, motorcycle and tank gave 16 200 bit-identical poses over 600 ticks in two threaded runs and a single-threaded one.
- **Sleep and waking:** the backend's vehicle does not wake its body on input, so step 2's waking is needed and enough (spike detail). The backend then keeps the chassis awake while forward input is held or the engine is above idle. Parking is the handbrake plus sleep; there is no anti-creep helper. The spike's handbraked car crept 0 mm on 10° and 13 mm on 20° before sleeping.
- **Teleports:** a chassis teleport is the existing body teleport. Wheel spin and engine RPM carry over, as a body's velocity does; wheel poses and their interpolation snap.
- **Rebuilds** (a kind change, a wheel-set change, `MotorcycleLean::maxAngle`, the chassis body rebuilt in one drain) carry over what still maps: engine RPM, gear, and each surviving wheel's spin. New wheels start still, the chassis keeps its velocity, and wheel poses snap.
- **Disabled or inactive:** `state` on the vehicle and its wheels resets, and wheel `Transform`s stay where they were. Re-enabling, or becoming valid again, builds the vehicle fresh with nothing carried over, so a car made kinematic for a cutscene restarts at idle in neutral.
- **Inactive scenes:** nothing new. A paused world steps no vehicle, keeps `state`, and poses no wheel. Destroying a world removes every vehicle with the constraints, before any body.

### FenrirPhysics: wheel poses and interpolation

- **Before the first step** a wheel entity sits at `position`.
- **From step 5 on,** after the chassis's write-back, physics writes each wheel entity's world position and rotation through `SetWorldTransform`. The pose is the chassis pose × the wheel's chassis-local pose: `position` raised by `compression`, then caster, camber and toe, then steer, then spin about the axle. With every angle 0, the wheel entity's rotation equals the chassis's, with its X as the axle, so left and right wheels can share a symmetric mesh. Scale is never written.
- **Output only:** game code's writes to a wheel's `Transform` are overwritten, so there is no edit detection for wheels, and physics never reads a mount point from it.
- **Interpolation** follows the chassis's `RigidBody::interpolate`, with no flag of its own, and `PhysicsConfig`'s app-wide switch covers it. Compression, steer and spin (angle unwrapped) are blended in chassis space between the last two steps, then composed with the chassis's blended pose in `AfterTicks`. `BeforeTicks` restores the true pose (the spike skipped this).

### FenrirPhysics: ground, grip and other bodies

- **Ground filtering:** each wheel's probe filters exactly as the chassis does, under the two-sided layer-and-mask rule of its `CollisionFilter`. There is no separate probe mask. Probes always skip triggers, the vehicle's own chassis and every character body. They ignore joints, as all queries do, so `collideConnected` never hides ground from a wheel.
- **Characters are never ground.** A car meeting a pedestrian meets them with its chassis, never by climbing a wheel up the capsule. To roll over something, make it a dynamic or static body.
- **Per-surface grip:** the tyre's curve value (or a track's `longitudinalFriction`/`lateralFriction`) combines with the ground's `PhysicsMaterial::friction` as the geometric mean, `sqrt(tyre × ground)`, the rule body contacts already use, so one `friction` means the same to bodies and tyres. The ground's friction is its body's material (FenrirPhysics gives each body one, a compound's being its own entity's or its first collider's), so the backend's default combine already does this (spike lesson 7). A per-sub-shape table is needed only once colliders get materials of their own. Ice must be set very low (0.05 under a 1.2 tyre still gives 0.24). No surface table, no vehicle-level friction; the chassis's own `PhysicsMaterial` is only what bodies touching the chassis feel. Rejected: the product (the default 0.5 material would halve every tyre).
- **Other bodies,** as the backend does them, with no new fields:
  - a wheel on a dynamic body pushes, loads and wakes it;
  - on kinematic or other moving ground, slip is measured against the ground's velocity at the contact, so a vehicle rides a platform through tyre grip; nothing like a character's carried velocity is needed;
  - the chassis is an ordinary dynamic body: hit, pushed, jointed and reported by contact events like any other;
  - queries see only the chassis and name its entity; `QueryFilter::ignoreBody` takes the chassis;
  - a body falling onto a wheel's drawn mesh passes through it and meets the chassis.
  - **Stated limit:** over light bodies the chassis dips, an artifact of the solver's mass ratio. A `probeMask` to filter rubble out of the probes alone can be added later.
- **Characters and vehicles,** following the character rules with no new behaviour: a character pushing a vehicle pushes with its `pushForce` (100 N by default), which barely moves a 1500 kg car; a vehicle hitting a character meets an immovable kinematic wall and stops (running people over is game code: layers that let the chassis pass, plus a contact event or `MoveCast` that writes knockback `velocity`); a character standing on a chassis rides it and presses on it with its weight.
- **Joints:** a `Joint` naming a wheel entity names a non-body and is inactive under the joints spec's edge rule.
- **General collision groups: not needed.** The driver is a disabled character; a trailer, turret or crane arm hinged to the chassis is excluded by its joint's `collideConnected`; wheels collide with nothing; teams of vehicles passing through each other is layers. General collision groups now wait on ragdolls alone.

### FenrirPhysics: edges

- **The chassis loses its body:** losing its `RigidBody` or usable collider, or turning kinematic or static, makes the vehicle inactive (one warning, `state` reset, wheel entities left where they were). Valid again, it is built fresh.
- **A chassis shape or centre-of-mass change** needs nothing: wheel positions are from the body origin, and the backend reads mass and inertia each step.
- **A chassis world-scale change** patches wheel positions in place as a live wheel edit, with no rebuild, and wakes the chassis.
- **Wheels destroyed mid-drive:** destroying or unlinking a wheel entity rebuilds the vehicle. A `VehicleDifferential` or `VehicleAntiRollBar` treats a wheel name that has *died* (the entity destroyed, or its `VehicleWheel` removed) as empty, with one warning; it is not made inactive.
  - A differential left with one wheel drives that wheel, so a rear-wheel-drive car that loses a rear wheel stays rear-wheel drive.
  - An anti-roll bar left with one wheel is inert.
  - **Stated limit:** a differential left with no wheels names no vehicle, so if it was a vehicle's only differential, that vehicle falls back to the all-wheel-drive default.
- **Robustness:** nothing beyond ADR-0015's inactive rules and the out-of-range rules: one warning per invalid setup, back once valid, never a crash.

### FenrirPhysics: trailers, getting in and out, flipping (recipes)

- **Towing needs no vehicle-specific joint rules:** a trailer is any dynamic body (usually a `TrailerVehicle`) joined to the chassis by an ordinary `Joint`, usually `ball` at the hitch. With the default `collideConnected = false` the hitch doesn't fight the chassis, and the joint links the two islands so they wake together. Trailer brakes are game code copying the tractor's `brake` (and `handbrake`) into the trailer's `input` each tick; there is no automatic link. A wheeled vehicle given no input also tows, staying in neutral (spike lesson 9). **Stated limit:** a trailer much heavier than its tractor jitters at the hitch.
- **Getting in and out is game code;** there is no enter or exit concept and no seat component. The recipe, in the `Vehicle` docs:
  - getting in: set the character's `CharacterBody::enabled = false`, which hides it from bodies, queries and triggers, then parent its entity under the chassis or a seat child at a seat-local `Transform`. A disabled character is never posed by physics and never part of the chassis compound (ADR-0014), so it follows the chassis's interpolated pose;
  - getting out: unparent it, place it beside the door, clear it with `Depenetrate` or `MoveCast`, then re-enable it.
- **Flipped and stuck vehicles are game code's;** there is no helper and `maxTilt` stays off by default. The recipe: notice `state.wheelsOnGround == 0` with the chassis's up turned away from world up for a while, then teleport the chassis upright (a `Transform` write) or apply a torque. A `maxTilt` of about 60° stops rollovers altogether.

### FenrirPhysics: the Jolt backend (private)

All in `src/Backend/Jolt/`; no public header names Jolt.
- The backend class gains vehicle operations (in the spike: `CreateVehicle`, `DestroyVehicle`, `UpdateVehicle`, `SetVehicleInput`, `ReadVehicle`, `WheelPose`).
- **The vehicle** is a `VehicleConstraint`, added with `AddConstraint` and `AddStepListener` and removed (both) before its body. Its controller is wheeled, motorcycle or tracked; a trailer is the wheeled controller with no differentials. Its collision tester is set before the first step from `probe`: the chassis body's own object layer from FenrirPhysics's private layer mapping, Jolt's default filters, and a body filter that drops the chassis and every character's inner body. Triggers are skipped by the probes themselves.
- **The only host callback** on job threads is that body filter; it reads data fixed before the step and never locks. Friction uses Jolt's default combine (spike lesson 7).
- **Suspension mapping (spike lesson 1, amending ticket 43):** Jolt's bump stop engages only below its min suspension length, and a probe starting inside the ground reports length 0, so with the hard point at the top of the travel a bottomed wheel never stops the chassis. The hard point sits a private margin (the wheel's radius) above the top of the travel, with min length = margin and max = travel + margin. `position` and `travel` keep their public meaning, and `compression` is clamped to `0..travel` in the read-back.
- **Motorcycle lean damping (spike lesson 2):** Jolt applies its lean damping in every velocity iteration and diverges once damping × collision-step time exceeds the chassis's roll inertia (16.7 kg m² at Jolt's 1000 and 60 Hz; the sample bike's box gives 10.4). The damping is capped privately at half the roll inertia ÷ the collision-step time; Jolt's spring of 5000 is kept. The cap is private, as the lean gains are.
- **Wheel settings** are each vehicle's own unshared copy, patched in place and validated by physics, because Jolt reads them by reference and its construction asserts don't re-run. The drivetrain, anti-roll bars, tracks, probe and tilt limit go through Jolt's writable references. The spike verified in-place patching (springs, radius, steer angle).
- **Built from the shape fields:** the torque curve on Jolt's `RPM / maxRpm` axis; each tyre's friction curve from its four numbers, slip angles converted to degrees; forward gear ratios spaced geometrically; the all-wheel-drive default as one single-wheel differential per wheel with `VehicleDifferential`'s defaults; torque shares normalised.
- **Input:** wheeled, motorcycle and trailer take forward, right, brake and handbrake; tracked takes forward, left ratio, right ratio and brake, with a 0 ratio replaced privately by a tiny one (Jolt refuses 0). Manual is `VehicleTransmission::Set(gear, clutch)`, with forward clamped to [0, 1].
- **Tracks:** Jolt's driven wheel on each track is the rearmost wheel of that side.
- **Priority** from the chassis entity's order. Jolt's vehicle drawing is compiled out; debug lines and the read-back come from its public getters.
- **Stated cost:** registering any vehicle stops Jolt's update from skipping a world where everything sleeps.

### FenrirPhysics: debug lines

- `DebugDraw::Vehicles = 1u << 6`, a new category included in `All`, with a C# mirror (`DebugDraw.Vehicles`, the same bit). For each wheel of each active, enabled vehicle, at step poses:
  - **the suspension:** a line from the top of the travel to the full-extension point (`position`), with a short cross-tick at the current wheel centre;
  - **the wheel:** a circle of its radius in the wheel's plane at its step pose, with one spoke so spin and steer show;
  - **the contact, while on ground:** a cross at the point and a line along the normal, coloured by grip: gripping, past `peakSlip`, past `slideSlip`. These, and the suspension and wheel colour, are new `DebugColour`s.
- Probe shapes are not drawn; the circle stands in for them. Disabled and inactive vehicles draw nothing, and `Colliders` still draws the chassis.

### FenrirPhysicsScripting and Fenrir.Physics

- **`Fenrir.Physics` contract:**
  - a new `Vehicles.cs`: `[NativeComponent("Fenrir.Physics.…")]` structs `Vehicle`, `VehicleWheel`, `VehicleDifferential` and `VehicleAntiRollBar`, writable, with C++'s fields in order and PascalCase and C++'s defaults when made with `new`;
    - `Vehicle`: `VehicleKind Kind = new()`, `VehicleInput Input`, `VehicleProbe Probe = Cylinder`, `float MaxTilt = π`, `bool Enabled = true`, `public readonly VehicleState State`;
    - `VehicleWheel`: `EntityLink Vehicle = EntityLink.None`, `Position`, `Track`, the settings in C++'s order, `public readonly VehicleWheelState State`;
    - `VehicleDifferential` and `VehicleAntiRollBar`: `EntityLink LeftWheel` and `RightWheel`, both `EntityLink.None` when new, then their settings;
  - `VehicleKind`, made with the generator: `[TaggedUnion(Noun = "vehicle")]`, tag `VehicleType : byte { Wheeled, Motorcycle, Tracked, Trailer }`, views `Wheeled`, `Motorcycle`, `Tracked` and `Trailer`, kinds `WheeledVehicle`, `MotorcycleVehicle`, `TrackedVehicle` and `TrailerVehicle` at offset 4; `TrailerVehicle` holds one unused byte; `new VehicleKind()` is wheeled;
  - the settings structs `VehicleEngine`, `VehicleTransmission` (with `TransmissionMode`), `VehicleTrack`, `MotorcycleLean`, `VehicleSuspension` (whose `Spring` is the existing `JointSpring`), `VehicleTyre` and `VehicleInput`, and the enums `VehicleProbe` and `VehicleTrackSide`, under C++'s names;
  - `VehicleState` and `VehicleWheelState` as `readonly struct`s with `readonly` fields; `VehicleWheelState.GroundBody` and `GroundCollider` are `EntityLink`s, `EntityLink.None` when new;
  - `DebugDraw.Vehicles` in `PhysicsWorld.cs`.
- **No native functions and no sugar.** Driving is field writes through `Ref<Vehicle>` (`Input.Forward`, `Input.Gear`, `Input.LeftTrack`). State is read in place through `ref readonly`; writing it doesn't compile. Getting in and out uses `CharacterBody.Enabled`, `Depenetrate` and `MoveCast`; flip recovery is a `Transform` write or the existing torque function. No convenience members (`IsGrounded`, km/h) and no C#-only `VehicleInput.FromSteer`. Vehicles add no `PhysicsNative` entries.
- **Bools** inside kinds and state (`MotorcycleLean.Enabled`, `LimitSteering`, `VehicleState.Shifting`, `VehicleWheelState.OnGround`) are read in place. `VehicleKind` and the states never appear in an event or a C++-to-C# call argument, so the README's bool caution does not apply.
- **Robustness:** only the foundation's and ADR-0015's rules. `default(Vehicle)` is all zeros: a disabled vehicle whose kind is the zero-tag wheeled kind with zeroed settings, harmless and silent. `default(VehicleWheel)` names entity 0 with no owner, so it is inactive with ADR-0015's one warning; likewise a differential or anti-roll bar made with `default`. Disabled and inactive vehicles read reset `State`. The doc comments say to make each with `new`. No extra warnings.
- **Doc comments** carry short forms of the recipes: on `Vehicle`, getting in and out, flipped vehicles and towing; on `VehicleWheel`, keeping wheel entities unscaled and out of the chassis's hierarchy and the chassis unscaled; on `VehicleWheel.State`, one collision step of lag, and skidding as the slips compared with `PeakSlip` and `SlideSlip`; on `VehicleInput`, that it persists, physics never writes it, clutch 0 is engaged, gear −1 is reverse, 0 neutral and 1…n forward, read only in Manual, and `Forward` is a pedal in Manual; on `VehicleSuspension.Spring`, writing the damping too.
- **`FenrirPhysicsScripting`:** the four component mappings; a plain stand-in mirror of `VehicleKind`'s private union, as for the other two; an `EntityLinkView` where each `Entity` sits (`vehicle`, `leftWheel`, `rightWheel`, `groundBody`, `groundCollider`). It adds no native functions, and FenrirPhysics itself still names nothing from Iris.

### DemoApp: the two F6 scenes, the chase camera and the gamepad

- **Switching:** F6 (`DemoFamilyKey::vehicles`) steps Default → "Vehicles (C++)" → "Vehicles (C#)" → Default through `DemoScenes`. Without scripting F6 toggles Default ⇄ Vehicles (C++). Each scene logs its key help on entry and registers its smoke keys. The F2 cycle gains "vehicles" before "all".
- **Camera:** each scene has a chase camera behind the driven vehicle (or the character on foot) writing `DemoCamera`, which `DemoCameras` applies, as in the character scenes. The C++ camera is the spike's; the C# scene has its own C# chase camera. C frees the camera (WASD flies it, the arrows still drive or walk) and C again snaps it back; F1 works as usual. Q/E are the shift keys, not orbit keys, because the chase camera turns with the vehicle.
- **Readouts,** in both scenes in the same format, every frame:
  - the driven chassis's `Name`: speed in km/h (the demo converts; the API stays m/s), RPM, gear, "shifting" while set, the clutch, wheels on ground, and "flipped" while the flip test holds; the motorcycle adds its lean, the tank its two track speeds; a trailer shows only speed and wheels on ground;
  - each wheel of the driven vehicle: the ground's `Name` or "in air", compression, longitudinal and lateral slip, and "sliding" while a slip is past the tyre's `slideSlip`;
  - L logs the full `state` of the driven vehicle and all its wheels. Parked vehicles keep their last names.
- **Wheel meshes:** unscaled wheel entities, not children of the chassis, each with a look child (a sphere scaled to the tyre plus a cube stripe); a radius change rescales the child. Chassis are unscaled with a scaled look child.
- **Flipped:** a vehicle is marked "flipped" once `state.wheelsOnGround` has been 0 for 2 s with the chassis's up more than 60° from world up. R rights the driven vehicle by the recipe (a `Transform` write upright, lifted 1 m, velocities zeroed). K kicks it over with a roll torque so R can be tested.
- **Gamepad, demo code only** (input mapping stays out of the spec; the engine gains nothing):
  - the DemoApp polls the first connected GLFW gamepad each frame and writes a demo `DemoGamepad { int Connected; Vec2 LeftStick; Vec2 RightStick; float LeftTrigger; float RightTrigger; uint32 Buttons; }` (`Demo.DemoGamepad`, registered in the demo's scripting extension) on an entity in the active scene: `DemoCamera`'s pattern turned around, C++ owning the device and C# reading it;
  - both vehicle scenes read it, and keyboard or pad both work; the pad's value wins beyond a deadzone of about 0.15, analog values going straight into `VehicleInput`:

    | Pad | Driving | On foot (C++) |
    |---|---|---|
    | Right trigger | `forward` | Not used |
    | Left trigger | `brake`, then reverse once stopped (as S does) | Not used |
    | Left stick X | `steer` | Walk (both axes) |
    | A | Handbrake | Jump |
    | Left / right bumper | Shift down / up | Not used |
    | Y | Get out (C++) | Get in |
    | Back | Right the vehicle | Not used |
    | Right stick | Orbit the chase camera, springing back behind on release | Same |

  - unplugging mid-drive falls back to the keyboard with no stuck input; the other scenes ignore the pad.
- **"Vehicles (C++)"**, the spike's `VehicleScene.hpp` rewritten against the final API with the spike's guesses dropped:
  - **keeps** the spike's track on tarmac (0.5): ten bumps, the 2 m ramp, the ice (0.05) and grass (0.3) lanes, the wall of 20 kg crates and the 15° parking slope; and its three vehicles: the 1500 kg car towing a 300 kg `TrailerVehicle` on a ball `Joint`, the 240 kg motorcycle with a rear-only differential, and the 4000 kg tank with nine wheels a side. Tab switches between them, moving the driver;
  - **adds** getting in and out as demo code following the recipe: the scene starts with the driver character seated in the car; F gets out (unparented beside the driver's door, cleared with `Depenetrate`, re-enabled; the chase camera follows it, and it walks with a small copy of the character scene's steering: WASD relative to the camera, Shift to run, Space to jump, no crouch); F within 3 m of any vehicle gets in (disabled, parented to that vehicle's seat child) and makes it the driven one. While nobody is driving, every vehicle gets handbrake 1, as parked vehicles always do;
  - **adds** a pedestrian (a standing character on the start straight, which the autopilot avoids); a ferry (a kinematic platform at 2 m/s shuttling across a gap); flip reset and kick (R/K); trailer brakes copied from the car's `brake` and `handbrake` every tick; and a rear `VehicleDifferential` with front and rear `VehicleAntiRollBar`s on the car;
  - **keys:**

    | Key | Driving | On foot |
    |---|---|---|
    | W / S, arrows | Throttle; S brakes, then reverses once stopped (the arrows also work while the camera is free) | Walk, relative to the camera |
    | A / D | Steer (the tank pivots when stopped) | Walk |
    | Space | Handbrake | Jump |
    | Shift | Hold the clutch | Run |
    | F | Get out | Get in the vehicle within 3 m |
    | Tab | Switch car / motorcycle / tank, moving the driver | Not used |
    | T | Teleport the driven vehicle to the start | Teleport the character to the start |
    | R / K | Right the vehicle / kick it over | Not used |
    | C | Free the camera, and re-attach it | Same |
    | M, E / Q | Manual gearbox, shift up / down | Not used |
    | 1–6 | Soft springs (live), big wheels (live), the motorcycle's max lean (a rebuild keeping RPM and gear), `enabled`, `interpolate`, the lean controller | Not used |
    | 7 / 8 / 9 | `maxTilt` off ⇄ 60°; destroy and remake the car's rear-left wheel (a rebuild each time, one dead-name warning from the rear differential); the car's chassis kinematic and back (one warning, then fresh at idle in neutral) | Same |
    | L | Log the state | Same |
    | P | Autopilot (gets the driver back into the car first) | Same |

- **"Vehicles (C#)"**, built only from C#; C++ places a `CSharpScene` marker with id 4 (`CSharpScenes::vehiclesId`):
  - a small track of its own: bumps, a grass patch and a ramp;
  - **one C# system builds everything:** the car's chassis, four wheel entities linked by `EntityLink`, a rear `VehicleDifferential`, two `VehicleAntiRollBar`s, and a `TrailerVehicle` on a ball `Joint`, whose brake is copied from the car each tick;
  - driving is field writes through `Input`; M swaps the gearbox to Manual through the kind's view, and E/Q shift;
  - readouts as in C++, the grass patch's name showing through the wheel's `GroundCollider`;
  - **keys:** the same letters for the same jobs as C++ (WASD and arrows, Space, Shift, C, T, R, K, M, E/Q, L, P), plus V toggling `DebugDraw.Vehicles` through `SetDebugDraw`, and U swapping the trailer's kind by assigning a new `VehicleKind` (trailer ⇄ wheeled with its default engine, given the car's `forward` so it pushes);
  - no motorcycle, tank, getting in and out, pedestrian or ferry: C# reaches those unchanged.
- **Autopilots and smoke runs:** each scene has a P autopilot, because smoke runs can only tap. The C++ route is the spike's lap (bumps, ramp, ice and grass, back to the start) extended over the ferry, avoiding the pedestrian; the C# route is a lap of its own track. Each smoke run presses P, about 40 L presses while it drives, then P again; taps each scene key twice to toggle it there and back; and presses K, R, F, F (out and back in) and T once each, in that order. `FENRIR_DEMO_SCENE` selects each scene as before. Smoke runs can't press the gamepad.

## Testing Decisions

- **What a good test is:** as in the foundation. It drives the engine through its public interface and asserts on component values (`input` untouched, `state`), world transforms (chassis and wheel entities), events read, query results and debug lines. Never on private state or which backend calls were made. Tests run against real Jolt. Same-build determinism lets tests assert exact results; speeds, distances and resting heights use a tolerance.
- **Seam 1, the App stepped one frame at a time with a chosen delta** (`FenrirPhysicsTests`):
  - the components: defaults; a vehicle inactive without a dynamic body, with no usable collider, beside a `CharacterBody`, with one warning per transition and reactivation; wheels inactive naming a non-vehicle, on the chassis entity, beside a `RigidBody`, `CharacterBody` or trigger `Collider`; a vehicle with no wheels inert; disabled vehicles a plain body with `state` reset and wheels left in place; differentials and anti-roll bars inactive for wrong names; out-of-range values clamped with one warning;
  - driving: the default car accelerating (0–100 km/h within the spike's 8.8 s, give or take) and braking to sleep; the all-wheel-drive default, and a rear `VehicleDifferential` driving only its wheels; torque shares normalised; geometric gear spacing measured in Manual (step ≈ 1.377); Manual gear and clutch echoed in `state` and a negative Manual `forward` clamped; Automatic shifting up and down; `steer` only on wheels with `maxSteerAngle`; the motorcycle standing at rest, leaning about 23° at 60 km/h under full steer, `limitSteering` capping steer, falling with `lean.enabled = false`; the tank pivoting on `leftTrack = −1, rightTrack = 1` with track speeds read back; a 0 track ratio holding the track; a trailer reading `brake` and `steer` and ignoring `forward`;
  - settings: live wheel edits (softer springs lower the chassis; a bigger radius raises it; a new `maxSteerAngle` steers at once); live drivetrain, probe and `maxTilt` edits; `maxTilt` at 60° stopping a roll; a bottomed-out wheel stopping the chassis (the private margin);
  - state: `VehicleState` and `VehicleWheelState` fields on a known drive; `compression` within `0..travel`; ground fields naming the floor's body and collider; ground fields empty in the air; reset when disabled or inactive; kept while asleep with spin 0;
  - grip: the spike's table within tolerance (ice spinning wheels with slip past `slideSlip`, grass slower than tarmac);
  - waking and parking: input and every component edit waking a sleeping chassis; a handbraked car on 10° creeping under 1 mm and sleeping;
  - teleports and rebuilds: a chassis teleport keeping RPM and spin and snapping wheel poses; a kind change, a wheel-set change and `maxAngle` keeping RPM, gear and surviving spin; a chassis body rebuilt in one drain carrying over; a chassis made kinematic and back starting fresh at idle in neutral; a chassis scale change moving wheel positions with no rebuild;
  - edges: a destroyed wheel rebuilding the vehicle; a differential with one dead wheel driving the other with one warning; an anti-roll bar with one dead wheel inert; a differential with no wheels left falling back to all-wheel drive; a `Joint` naming a wheel inactive;
  - other bodies: probes filtering by the chassis's `CollisionFilter`, skipping triggers, the chassis and a character's inner body; a car driven at a character stopped by the chassis with no wheel on the capsule; a wheel on a dynamic box pushing it; a parked vehicle riding a kinematic platform; queries naming the chassis and `ignoreBody` skipping it; a trailer on a ball joint towed and waking with the tractor;
  - wheel poses: before the first step at `position`; the composed pose with every angle 0 matching the chassis's rotation, X as the axle; caster, camber and toe mirrored by side; scale never written; a game write to a wheel `Transform` overwritten; at frames of 0.3 ticks, the blended wheel pose on its axle relative to the blended chassis within 1e-4; `BeforeTicks` restoring the true pose;
  - determinism: a car, a motorcycle and a tank over bumps and crates, bit-identical over 600 ticks in two runs, single- and multi-threaded;
  - paused scenes changing nothing; destroying a world with vehicles;
  - `Vehicles` debug lines: counts for a known vehicle, contact colours by slip, nothing for disabled or inactive vehicles.

  Prior art: the foundation's `PhysicsTestApp` and `PhysicsProbe`, the joints' and characters' cases, and the spike's eleven `[vehicle-spike]` cases on the prototype branch.
- **Seam 2, the scripting fixture harness:**
  - **generator tests** (the prerequisite; their full list belongs to the generator ticket): a `TaggedUnionControl OK` case; one compile-fail case per diagnostic; an empty kind; `CollisionShape` and `JointKind` on the generator with their existing tests unchanged; `Tests/Fixture/Shapes.cs` on the generator with its four `ScriptingTests` cases unchanged;
  - in `PhysicsScriptingTests` with a `Vehicles.cs` fixture: the four components validate against their native layouts; each `new` equals C++'s `{}`, including `new VehicleKind()`, the empty links and `MaxTilt = π`; a C# system builds a chassis and four wheel entities linked in that same system, and after playback `Input.Forward = 1` raises `State.ForwardSpeed`; `Kind.Wheeled.Transmission.Mode = Manual` then `Input.Gear = 2` shows in `State.Gear`; assigning a new kind (wheeled to trailer) reaches physics; the wrong view throws; `VehicleWheel.State.OnGround` is true and `GroundBody` names the floor; a `VehicleDifferential` built from C# drives only its wheels; `DebugDraw.Vehicles` equals the C++ bit;
  - compile-fail cases: writing `Vehicle.State` or `VehicleWheel.State`, or a field of either, through `Ref<T>` doesn't compile.

  Prior art: the foundation's, joints' and characters' `PhysicsScriptingTests` and fixtures, `ScriptingProbe`, `HeadlessApp`, the `fenrir_expect_compile` cases, and the existing generator tests.
- **FenrirECS:** nothing new.
- **The DemoApp scenes are verified by hand,** as the spikes were:
  - **Switching and smoke runs:** F6 steps Default → Vehicles (C++) → Vehicles (C#) → Default, logging each scene's key help; the F2 mode carries across a switch, and "vehicles" draws the suspension, the wheel circles and contacts coloured by slip; `FENRIR_DEMO_SCENE` with `FENRIR_DEMO_KEYS=1` runs each scene with no errors, in Debug and Release, the only warnings being the expected one for each of 8 and 9; without scripting F6 toggles Default ⇄ Vehicles (C++).
  - **Camera (both):** it chases the vehicle; C frees it (WASD flies, the arrows still drive) and C again snaps it back.
  - **Wheels:** the meshes sit on their axles, spin (the stripe), steer and compress over the bumps; with 5 off they still move with the chassis.
  - **Gamepad (both, with a pad connected):** analog throttle, brake and steer drive; unplugging mid-drive falls back to the keyboard with no stuck input.
  - **Vehicles (C++):**
    - **car:** about 100 km/h on the straight; parks on the 15° slope with Space without creeping; on ice its wheels spin ("sliding", contacts coloured past `slideSlip`), and grass is looser than tarmac; M with E/Q shifts by hand;
    - **trailer:** towed over the ramp, it brakes with the car;
    - **motorcycle:** stands at rest and leans into turns; with 6 off it falls over; 3 changes the max lean without losing RPM or gear;
    - **tank:** pivots on the spot;
    - **crates:** the car shoves them aside, with the chassis dip as stated;
    - **pedestrian:** stops the car at the chassis, and the wheels never climb it;
    - **ferry:** a parked vehicle rides it;
    - **getting in and out:** F gets out beside the door, not inside anything, and the character walks; F near the tank gets in and drives it; Tab moves the driver;
    - **flip and tilt:** K flips the car, "flipped" shows, and R rights it; with 7 on, K can't roll it past 60°;
    - **live edits:** 1 and 2 change the ride height live; with 4 off the car is a plain rolling box;
    - **wheel destroyed:** 8 gives one warning and the car stays rear-wheel drive on its one rear wheel; 8 again restores it;
    - **kinematic:** 9 gives one warning, and on return the car is at idle in neutral;
    - **readouts:** the names show every readout live.
  - **Vehicles (C#):** C# builds the scene with no warning; the car drives and its trailer brakes with it; M and E/Q shift by hand; the readouts match the C++ format and the grass shows in the wheel's readout; V toggles the debug lines; K and R work as in C++; U turns the trailer into a wheeled car that pushes, and U again turns it back.

## Out of Scope

- Input mapping (the demo's gamepad driving is demo code), vehicle audio, AI driving, damage and destruction, and network prediction.
- Wheels as bodies, wheel colliders, or anything colliding with a wheel.
- Up and forward axis fields (addable later, defaulting to the convention), kingpin and sideways suspension tilt.
- Free-form torque and tyre curves, gear lists, a surface friction table and a vehicle-level friction.
- A `probeMask` separate from the chassis's `CollisionFilter`.
- A per-sub-shape friction table, until colliders get materials of their own.
- A centre-of-mass offset (beyond vehicles).
- An anti-creep helper.
- Wheel or vehicle events (landing, skidding, surface changes).
- An enter or exit concept, a seat component, a flip helper, and automatic trailer brakes.
- General collision groups: with ragdolls.
- Probe frequency and the other tuning refused under ADR-0010.
- Gamepad input in the other DemoApp scenes, and an engine input layer.
- Ragdolls and soft bodies: each its own spec.
- A separate demo app per area, and an on-screen text overlay in the DemoApp.

## Further Notes

- **Settled by the generator ticket** (`.scratch/fenrir-scripting-tagged-union/issues/01-tagged-union-generator.md`): the field-name rule has no override, so moving `CollisionShape` renames `_hull` to `_convexHull` in C# and in its one native mapping string (generator build ticket 04). It also allows any enum as the tag, drops ticket 47's `Offset =` and adds an optional `Noun` for the wrong-view message.
- **ADR-0015 is amended by the first build ticket,** as `CODING_STANDARDS.md` asks of a change that makes an ADR false:
  - `VehicleKind` has four alternatives, adding `TrailerVehicle` (ticket 45);
  - the left-or-right field is `VehicleTrackSide track`, since `VehicleTrack` is the per-track settings struct (ticket 43; the spike's name);
  - the track ratios are in the shared `VehicleInput` with defaults of 1, not in the tracked alternative (ticket 43);
  - wheel entities are kept out of the chassis's hierarchy, not "usually under the chassis" (spike lesson 5, ticket 48);
  - `Vehicle` gains `probe`, `maxTilt` and `state`, and `VehicleWheel` its settings and `state` (tickets 43 and 44).
- **Decision history:**
  - the wayfinder map `.scratch/fenrir-physics/map.md` and its vehicle tickets 40–48;
  - ADR-0015, with ADR-0009 to ADR-0014 still applying;
  - research branches `research/jolt-vehicles` (`c891ac5`) and `research/engine-vehicle-models` (`7e98980`);
  - the spike branch `prototype/fenrir-physics-vehicles-spike` (`a5c6d8c`);
  - the generator's own ticket, `.scratch/fenrir-scripting-tagged-union/issues/01-tagged-union-generator.md` (resolved), and its build tickets 02–04.
- **Where tickets disagreed,** the later one is followed:
  - the probe defaults to `Cylinder` for every kind (46, over 43's `Ray` for tracked);
  - the hard point sits a private margin above the travel, min length = margin (46, over 43's min length 0 at the top of the travel);
  - grip uses the backend's default combine on the body's one material, with no private sub-shape table (46, over 44's table); `groundCollider` is still found by sub-shape;
  - trailers are a `TrailerVehicle` kind (45); the spike's no-input wheeled vehicle also tows (46, lesson 9, provisional);
  - `Rpm`, not `RPM` (47, over 43);
  - `VehicleSuspension::spring` stays `JointSpring`, with a doc note on its damping default (47, settling 46's lesson 4 between that and a spring type of its own);
  - the tracked kind's engine and gearbox numbers not given by 43 (peak torque RPM, first and top gears) and the motorcycle's peak at 6000 RPM are the spike's (47 leaves values to the spec);
  - wheel entities are not children of the chassis (46 and 48, over ADR-0015's "usually under the chassis");
  - a backend vehicle is created after its body in step 1 and its edits pushed in step 2 (45, with 44 and the spike, which made it in step 2).
- **Risks:**
  - The suspension margin and the lean-damping cap are private fixes to backend behaviour; test the bottomed wheel and a light bike landing early.
  - The backend marks motorcycles as still in development; a Jolt upgrade may change lean behaviour.
  - Wheel settings patched in place rely on undocumented backend behaviour (settings read by reference each step); a Jolt upgrade could break it. The spike checked springs, radius and steer.
  - Registering any vehicle stops the backend skipping an all-asleep world's update, so a parked car costs a little every tick.
  - The probe body filter runs on job threads and must never lock.
  - `BeforeTicks` restoring wheel poses, trailers, out-of-range warnings, inactive warnings for differentials and anti-roll bars, the debug lines and C# were not built by the spike.
  - MSVC and Linux were not built by the spike.
  - The generator prerequisite touches FenrirScripting core and both existing unions; land and test it first.
- **Engine changes land outside FenrirPhysics:** FenrirScripting (the `[TaggedUnion]` generator, its README section, and `CollisionShape` and `JointKind` moved onto it; `CollisionShape` lives in FenrirPhysics's contract) and the DemoApp (`DemoGamepad`, `CSharpScenes::vehiclesId`, two scenes, the F2 entry). The generator is independently useful and can be built first.
- **Vocabulary** follows `GLOSSARY.md`: vehicle, chassis, wheel (wheel entity), track, vehicle kind, trailer, driver input, drivetrain, differential, anti-roll bar, suspension travel, compression, slip, tagged union, plus the foundation's, joints' and characters' terms. Avoid "vehicle constraint" and "vehicle controller" (the backend's words), "car" for a vehicle in general, "hull" for the chassis, "vehicle body", "tyre" for the whole wheel, "wheel collider", "tank" for the kind, "unpowered" or "towed vehicle" for a trailer, "diff", "axle" for a differential, "suspension length" and "ride height" for compression, and "skid" or "drift" for slip. "Hard point", "suspension margin" and "inner body" are private backend vocabulary, never in a public doc comment.
