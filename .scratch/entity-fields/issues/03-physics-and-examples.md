# 03: Physics and the examples hold an `Entity`

**What to build:** physics's regenerated holders and every script that spelled `EntityLink` (spec: "The built-in binding, physics, scripts", second, fourth and fifth bullets).

- **`fenrir-generate-bindings`**: physics regenerates through `uses` with no configuration change. Expected diff in `packages/FenrirPhysics/Managed/Fenrir.Physics/Generated/`: `Joint`, `VehicleWheel`, `VehicleDifferential`, `VehicleAntiRollBar` show `public Entity` properties with getter and setter over private `EntityLink` backing fields; `CharacterState`, `VehicleWheelState` and the five events show getter-only properties over `private readonly` backing fields. The mappings header, the registration and the defaults tests are byte-identical. Anything else is a stop: keep the change uncommitted, and say what differed.
- **`packages/FenrirPhysics/Managed/Fenrir.Physics/Joints.cs`, `Vehicles.cs`**: the hand-written docs name `Entity.None` where they named `EntityLink.None`, and say `default(Joint)`'s links name entity 0 as before.
- **`packages/FenrirPhysics/Scripting/Tests/`**: the compile-fail cases still pass (the state structs are unchanged); the physics scripting fixture (`Fixture/`) drops any `EntityLink` spelling for `Entity`.
- **`examples/PhysicsDemo/scripts/Character.cs`, `Joints.cs`, `Vehicles.cs`, `examples/RendererDemo/scripts/MeshesAndMaterials.cs`**: `NameOf(EntityLink)` helpers go (the `Entity` overload serves); `BodyB = EntityLink.None` is `BodyB = Entity.None`; `Relationship` walks read `.FirstChild` as an `Entity`. The examples build is the user's check (sessions leave DemoApp alone): say which files changed.

**Blocked by:** 02

**Touches:** `packages/FenrirPhysics/Managed/`, `packages/FenrirPhysics/Scripting/Tests/Fixture/`, `examples/PhysicsDemo/scripts/`, `examples/RendererDemo/scripts/`

**Status:** ready-for-agent

- [ ] `grep -rn "EntityLink" packages/*/Managed/*/Generated/` finds only private backing fields; `grep -rn "EntityLink" examples/*/scripts/` finds nothing.
- [ ] `ctest -R "Scripting|PhysicsCompile"` passes under the msvc-x64-debug preset; both generators are no-ops afterwards.
- [ ] clang-format and `check_tickets.py` pass.

## Comments

Filed 2026-10-28 from the spec. Cut from ticket 02's pushed branch.
