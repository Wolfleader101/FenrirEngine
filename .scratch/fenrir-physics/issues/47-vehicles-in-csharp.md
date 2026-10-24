# What does C# get for vehicles?

Type: grilling
Status: resolved
Blocked by: 42, 43, 44, 45

## Question

What does the physics scripting extension (`FenrirPhysicsScripting`, contract `Fenrir.Physics`) give C# for vehicles? Builds on [How is a vehicle modelled in the ECS?](42-vehicles-in-the-ecs.md), [Which settings does each vehicle kind expose?](43-vehicle-kinds-and-settings.md), [How does a vehicle step each tick, and what does game code read back?](44-how-a-vehicle-steps-and-reports.md) and [How does a vehicle meet other bodies, joints and characters?](45-vehicles-and-other-bodies.md), and follows "What does C# get for joints?" and "What does C# get for characters?".

- **Components:** the vehicle component (and wheel components, if wheels are entities) writable from C#; if the kind is a tagged union, whether it is hand-written like `CollisionShape` and `JointKind` or is the third union that triggers the `[TaggedUnion]` generator.
- **Arrays:** if wheels, gears or torque-curve points are arrays inside a component, how a C# component maps a fixed-size array (FenrirScripting may need a prerequisite, as read-only fields did for characters).
- **Driving:** driver input as field writes, so no native function.
- **State:** read-only in C# if written back (as `CharacterState` is), or native queries in the foundation's pattern.
- **Robustness:** the foundation's no-op-with-warning rules.
- **What the C# demo scene needs** that tests alone would not prove.

From "Which settings does each vehicle kind expose?": no vehicle component holds an array (curves are shape fields, gears are generated), so the **Arrays** bullet needs no FenrirScripting prerequisite. Two more components to map: `VehicleDifferential` and `VehicleAntiRollBar`, each naming two wheel entities. `VehicleInput` gains `gear`, `clutch`, `leftTrack` and `rightTrack` (track ratios default to 1).

From "How does a vehicle step each tick, and what does game code read back?": `Vehicle` holds a `VehicleState state` and `VehicleWheel` a `VehicleWheelState state`, both written back by physics each tick, so both are read-only in C# through the `ref readonly` accessor that "What does C# get for characters?" added. No native state query is needed.

## Answer

**C# gets every vehicle component as a straight, writable mirror, with no native functions.** The one new piece is the **`[TaggedUnion]` generator**: `VehicleKind` is the third engine union, which is the fog entry's trigger. So the generator is built as a FenrirScripting prerequisite of the vehicles spec, and `CollisionShape` and `JointKind` move onto it. There is no ADR, because the generator emits exactly the hand-written surface, so it can be removed by pasting that code back. Every mirror rule follows ADR-0009, ADR-0015, "What does C# get for joints?" and "What does C# get for characters?". Glossary: **tagged union** added.

**Facts found in the code** (on `dotnet-scripting`):
- `JointKind` is hand-written:
  - a private byte tag at offset 0 and each kind at `FieldOffset(4)`, with private fields named after the tag enum's members (`_hinge` ↔ `JointType.Hinge`);
  - `[UnscopedRef] ref` views, implicit conversions and a private `Expect` that throws.
- The default kind is the tag enum's first member, both in `JointKind` (`Fixed`) and in `CollisionShape` (`Box`).
- Every `VehicleKind` alternative holds values of at most 4 bytes, so its kinds also sit at offset 4.
- `ComponentFieldsGenerator` gives each field a `ref` accessor on `Ref<T>`, so `vehicle.Input.Forward = 1` writes in place. A `readonly` field gets `ref readonly` (the characters prerequisite, already built).
- `PhysicsWorld.SetDebugDraw(DebugDraw)` takes a C# flags enum.
- On the native side, each union is mapped through a hand-written plain stand-in, because FenrirPhysics names nothing from Iris. A C# generator leaves that unchanged.
- No vehicle component holds an array ("Which settings does each vehicle kind expose?"), so the **Arrays** bullet needs nothing.

### Decisions

1. **`VehicleKind` triggers the `[TaggedUnion]` generator.** It is built now, as a FenrirScripting prerequisite ticket of the vehicles spec, as the joints spec listed `EntityLink` and the character spec listed read-only fields. `CollisionShape` and `JointKind` move onto it in the same ticket, and their existing tests pass unchanged. That proves the surface is the same. The map's fog entry now says the trigger fired and points here, and the FenrirScripting README's "Tagged unions" section is rewritten around the generator.
   - Rejected: building it but leaving the two existing unions hand-written, which keeps two ways of writing the same thing.
   - Rejected: hand-writing a third union and pushing the trigger out again.
2. **What a union author writes, and what the generator emits.**
   - **The author writes:**
     - the tag enum, backed by `byte`;
     - `[TaggedUnion(Offset = 4)]` on a `[StructLayout(LayoutKind.Explicit)] public partial struct`;
     - the private fields: `[FieldOffset(0)]` for the tag and `[FieldOffset(Offset)]` for one field per kind, named `_` plus the enum member in camelCase.

     The author keeps the fields because the native mapping (`IRIS_FIELD_AS`) and the layout check name them, so moving the two existing unions over changes no native code. The offset is spelled out because C# can't see the C++ union's alignment, and a wrong one is already refused by the layout check at load.
   - **The generator emits** the members, pairing each field with the enum member of the same name:
     - a parameterless constructor setting the kind with tag 0 to that kind's `new()`, so `new()` and `default` agree on the kind;
     - `Type`;
     - one `[UnscopedRef] ref` view per kind, named after the enum member;
     - an implicit conversion from each kind;
     - `InvalidOperationException` on the wrong view, with the existing message shape.
   - **Build errors** (generator diagnostics): the struct is not partial; a field has no matching enum member or an enum member has no field; a kind field is not at `Offset`, or the tag is not at 0; a kind is not unmanaged; the enum is not backed by `byte`.
   - Script authors can use it as well. That was the trigger's other half.
   - Rejected: the generator emitting the fields too, from a list of types, which would fix the native side's names by a rule.
3. **RPM is spelled `Rpm` in both languages.** In C++ that is `minRpm`, `maxRpm`, `peakTorqueRpm`, `torqueAtMinRpm`, `torqueAtMaxRpm`, `shiftUpRpm`, `shiftDownRpm` and `engineRpm`. In C# it is `MinRpm`, …, `EngineRpm`. This amends "Which settings does each vehicle kind expose?"'s `RPM` spellings in the spec, so both sides agree with 44's `engineRpm` and with .NET's PascalCase rule for acronyms over two letters.
4. **The C# components are a straight mirror.** They are `[NativeComponent("Fenrir.Physics.…")]` structs, writable, with C++'s field order in PascalCase and C++'s defaults when made with `new`. There are no convenience members (`IsGrounded`, km/h).
   - **`Vehicle`:** `VehicleKind Kind = new()`, `VehicleInput Input`, `VehicleProbe Probe`, `float MaxTilt = π`, `bool Enabled = true`, and `public readonly VehicleState State`.
   - **`VehicleWheel`:** `EntityLink Vehicle = EntityLink.None`, `Position`, `Track`, then the settings (`Radius`, `Width`, `Suspension`, the tyres, `Caster`, `Camber`, `Toe` and the rest), and `public readonly VehicleWheelState State`.
   - **`VehicleDifferential` and `VehicleAntiRollBar`:** `EntityLink LeftWheel` and `RightWheel`, both `EntityLink.None` when new, then their settings.
   - **`VehicleKind`:** made with the generator. The tag is `VehicleType : byte { Wheeled, Motorcycle, Tracked, Trailer }`, the views are `Wheeled`, `Motorcycle`, `Tracked` and `Trailer`, and the kinds are `WheeledVehicle`, `MotorcycleVehicle`, `TrackedVehicle` and `TrailerVehicle`. `TrailerVehicle` holds one unused byte for the empty C++ struct, as `FixedJoint` does. `new VehicleKind()` is wheeled, C++'s default kind.
   - **Settings structs:** `VehicleEngine`, `VehicleTransmission` (with its mode enum), `VehicleTrack`, `MotorcycleLean`, `VehicleSuspension`, `VehicleTyre`, `VehicleInput` and the probe enum, under C++'s names. `VehicleSuspension.Spring` is the existing `JointSpring`.
   - **State:** `VehicleState` and `VehicleWheelState` are `readonly struct`s with `readonly` fields. `VehicleWheelState.GroundBody` and `GroundCollider` are `EntityLink`s, `EntityLink.None` when new. C# reads both states in place through `ref readonly`, and writing them doesn't compile.
   - Bools inside kinds and state (`MotorcycleLean.Enabled`, `VehicleState.Shifting`, `VehicleWheelState.OnGround`) are read in place. None of these structs crosses from C++ by value, so the README's caution about bools does not apply. The spec states that `VehicleKind` never appears in an event or in a C++-to-C# call argument.
5. **No native functions and no sugar.**
   - Driving is field writes through `Ref<Vehicle>` (`Input.Forward`, `Input.Gear`, `Input.LeftTrack`).
   - State is read in place, so no state query is needed.
   - Getting in and out uses the existing `CharacterBody.Enabled`, `Depenetrate` and `MoveCast`.
   - Flip recovery is a `Transform` write or the existing torque function.
   - Turning steer into track ratios stays game code. There is no C#-only `VehicleInput.FromSteer` (the precedent of characters' decision 7).
   - Vehicles add no `PhysicsNative` entries.
6. **`DebugDraw.Vehicles`:** C#'s flags enum gains the vehicle debug-line category, with the same bit as C++, so `SetDebugDraw` takes it unchanged. What it draws is the spec's or 48's.
7. **Robustness: only the foundation's and ADR-0015's rules apply.**
   - With no native functions, there is nothing new to no-op.
   - `default(Vehicle)` is all zeros: a disabled vehicle (`Enabled = false`) whose kind is the zero-tag wheeled kind with zeroed settings. It is harmless and gives no warning.
   - `default(VehicleWheel)` names entity 0 with no owner, so it is inactive with ADR-0015's one warning. The same goes for a differential or anti-roll bar made with `default`.
   - Disabled and inactive vehicles read reset `State`, as in C++.
   - The component doc comments say to make each with `new`. No extra warnings are added.
8. **Doc-comment notes C# carries.** The spec's recipes stay the source, and these are short forms of them:
   - **`Vehicle`:** getting in and out (disable the character, parent it to a seat; on the way out, unparent it and clear it with `Depenetrate` or `MoveCast` before re-enabling it). Flipped vehicles (`State.WheelsOnGround == 0` with the chassis's up turned away for a while, then a `Transform` write or a torque, or `MaxTilt` of about 60° to stop rollovers). Towing (copy the tractor's `Input.Brake` into the trailer's `Input` each tick).
   - **`VehicleWheel.State`:** one collision step of lag in contact, compression and steer; skidding is the slips compared with `VehicleTyre`'s `PeakSlip` and `SlideSlip`.
   - **`VehicleInput`:** it persists and physics never writes it; clutch 0 is engaged; gear −1 is reverse, 0 neutral and 1…n forward, read only in Manual.
9. **Spike-dependent details are not decided here.** The rule is that C# mirrors C++'s fields and defaults, checked by the layout validation and a test that `new` equals `{}`. Every value, and any field the spike (46) adds or changes, is the spec's, so nothing here goes stale when the spike lands.
10. **Module shape.**
    - **FenrirScripting core (prerequisite ticket):**
      - `TaggedUnionAttribute` in `Fenrir.Scripting`;
      - `TaggedUnionGenerator` in `Fenrir.Scripting.Generators`;
      - generator tests, including each diagnostic;
      - the README rewrite;
      - `CollisionShape` and `JointKind` moved onto it, with their tests unchanged.
    - **`Fenrir.Physics` contract:**
      - a new `Vehicles.cs` with the four components, `VehicleKind`, the kinds, the settings structs, both state structs and the enums;
      - `DebugDraw.Vehicles` in `PhysicsWorld.cs`.
    - **`FenrirPhysicsScripting`:**
      - the four component mappings;
      - a plain stand-in mirror of `VehicleKind`'s private union, as for the other two;
      - an `EntityLinkView` where each `Entity` sits (`vehicle`, `leftWheel`, `rightWheel`, `groundBody`, `groundCollider`).

      It adds no native functions, and FenrirPhysics itself still names nothing from Iris.
11. **Tests:** new `PhysicsScriptingTests` cases with a `Vehicles.cs` fixture.
    - The four components validate against their native layouts.
    - Each `new` equals C++'s `{}`, including `new VehicleKind()`, the empty links and `MaxTilt = π`.
    - A C# system builds a chassis and four wheel entities linked in that same system, and after playback the vehicle drives: `Input.Forward = 1` raises `State.ForwardSpeed`.
    - Editing through a view (`Kind.Wheeled.Transmission.Mode = Manual`, then `Input.Gear = 2`) shows in `State.Gear`.
    - Assigning a new kind (wheeled to trailer) reaches physics.
    - The wrong view throws.
    - `VehicleWheel.State.OnGround` is true and `GroundBody` names the floor.
    - A `VehicleDifferential` built from C# drives only its wheels.
    - `DebugDraw.Vehicles` equals the C++ bit.
    - Compile-fail cases: writing `Vehicle.State` or `VehicleWheel.State` (or a field of either) through `Ref<T>` doesn't compile, as for `CharacterState`.
12. **What the C#-built scene must exercise** (input to "What do the vehicle demo scenes show?", which decides the contents and keys):
    - C++ places only a marker. C# builds everything in one system: the chassis, wheel entities linked by `EntityLink`, a differential and an anti-roll bar, and a trailer on a ball `Joint`, with the tractor's `Brake` copied each tick.
    - Keys drive through `Input`.
    - A key swaps the gearbox to Manual through the view, and two keys shift.
    - A readout every frame shows speed, RPM and gear from `State`, and one wheel's compression and slip.
    - Wheel meshes are drawn from the written-back poses.
    - A key toggles `DebugDraw.Vehicles`.
    - A flip-reset key follows the recipe.
    - Not getting in and out, which stays the C++ scene's.

## Comments

- 2026-10-20: The generator ticket (`.scratch/fenrir-scripting-tagged-union/issues/01-tagged-union-generator.md`) amends decision 2: the tag may be any enum, the attribute has no `Offset` (the fields' `[FieldOffset]` is the only copy) but takes an optional `Noun` for the wrong-view message, and `CollisionShape`'s `_hull` is renamed `_convexHull` on both sides, so the move changes one native string.
