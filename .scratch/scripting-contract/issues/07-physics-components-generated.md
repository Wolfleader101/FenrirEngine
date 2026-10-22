# 07: Physics components, events and shared structs generated

**What to build:** candidate E for FenrirPhysics (spec: "Stage 2", "What does not change"): the hand-written C# mirrors, the twenty stand-ins, the hand mappings, the registration lists and the defaults tests are replaced by what ticket 06's tool generates from ticket 05's description. `Fenrir.Physics`'s public surface does not change.

- **Binding configuration** `packages/FenrirPhysics/Scripting/api/FenrirPhysics.binding.toml`: namespace `Fenrir.Physics`; bound types `Math::Vec3` → `System.Numerics.Vector3`, `Math::Quat` → `System.Numerics.Quaternion`, `Fenrir::Transform` → `Fenrir.Scripting.Transform`, `Fenrir::Entity` → `Fenrir.Scripting.EntityLink`; rename exceptions as needed so every public C# name stays as it is today (`hull` → `ConvexHull` in `CollisionShape`'s storage, and any others the diff below finds); `Generated/` output into `Managed/Fenrir.Physics/Generated/`, `Scripting/src/Generated/` and `Scripting/Tests/Generated/`.
- **`iris_generate_binding(FenrirPhysics ...)`** in `Scripting/CMakeLists.txt`; the generated sources join the contract's glob, the extension's library and its tests.
- **The C# contract**: `Components.cs`, `CollisionShape.cs`, `Joints.cs`, `Characters.cs`, `Vehicles.cs`, `Events.cs`, `Queries.cs` keep only behaviour as second `partial` parts: constructors (`Collider(CollisionShape)`), `IEquatable<CollisionMesh>` and its operators, the tagged unions' `Noun`, helper properties, and type-level remarks that are not the C++ doc. Every field, field initialiser, `public T() {}`, `[FieldOffset]`, `[StructLayout]`, `[NativeComponent]` and event attribute moves to the generated part. `FixedJoint`, `BallJoint` and `TrailerVehicle` lose `_unused`.
- **The extension**: every `Detail::*Layout` stand-in, every hand-written `Iris::StructMapping` and the `extension.components`/`events` lists go from `PhysicsScripting.cpp`; the generated mapping and registration sources replace them; `extension.sharedStructs` is the generated list (new: today nothing validates these). `EngineMappings.hpp`'s `EntityLinkView` is no longer referenced by physics (ticket 09 removes it).
- **Tests**: the generated defaults test replaces the hand-written defaults cases (Joint at `PhysicsScriptingTests.cpp:936`, CharacterBody `:1681`, Vehicle `:2039`, wheel/differential/anti-roll bar `:2074`, RigidBody `:176`) and the tests' twelve `Same()` copies (the engine's `Same()` overloads in `src/Same.hpp` and `VehicleSettings.hpp` stay; the duplicate `Same(JointSpring)` goes). The existing `ValidateStruct` cases for query mirrors go: the load-time shared-struct check covers them.
- **Public API diff**: build the pre-ticket `Fenrir.Physics.dll` and the new one and diff their public surfaces (e.g. with `Microsoft.DotNet.ApiCompat` or a reflection dump committed to the PR); the only differences allowed are the three removed private fields. Record the diff in the PR.
- `Scripting/CMakeLists.txt`'s and `README.md`'s comments describe the generated layout and how to regenerate.

**Blocked by:** 03 (the extension's shape), 05 (the description), 06 (the generator).

**Touches:** `packages/FenrirPhysics/Scripting/api/`, `packages/FenrirPhysics/Scripting/CMakeLists.txt`, `packages/FenrirPhysics/Scripting/src/`, `packages/FenrirPhysics/Scripting/Tests/`, `packages/FenrirPhysics/Managed/Fenrir.Physics/`, `packages/FenrirPhysics/README.md`.

**Status:** ready-for-agent

- [ ] `grep -rn "Layout\b\|IRIS_FIELD" packages/FenrirPhysics/Scripting/src` finds only generated files; no hand-written `StructMapping` remains in `PhysicsScripting.cpp`.
- [ ] The generated defaults test covers every component, event and shared struct of the extension and passes; the listed hand-written defaults cases and `Same()` copies are deleted; `FenrirPhysicsScriptingTests` otherwise passes unchanged, including the CompileFail cases and the fixture scripts, which are not edited.
- [ ] The public API diff of `Fenrir.Physics.dll` shows only `FixedJoint._unused`, `BallJoint._unused` and `TrailerVehicle._unused` removed; the DemoApp's C# scripts build unchanged and its physics smoke run passes.
- [ ] `fenrir-describe-api` then `fenrir-generate-bindings` leaves the tree clean.
