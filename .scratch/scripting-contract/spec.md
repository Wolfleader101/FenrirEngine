Status: ready-for-agent

# The scripting contract: each shared type described once, and the physics extension's back door closed

Origin: the second FenrirPhysics architecture review left two candidates, E ("describe each component once") and H ("close the scripting extension's back door"). A grilling of 32 questions over three rounds settled them together, after research into how s&box, Coral and Mono share structs and functions with C# (`research/sbox-csharp-cpp-interop.md` in this folder, also on branch `research/sbox-csharp-cpp-interop`). ADR-0016 (packages share plain data through a generated, language-neutral API description) records the hard-to-reverse call; ADR-0006 (the Iris API freeze) and ADR-0009 (extensions) are amended. ADR-0004 (shared contract assemblies), ADR-0007 (ECS-first scripting) and ADR-0011 (physics components are plain data) still apply. The vocabulary is in `GLOSSARY.md`: *API description*, *bound type*, *binding*, *extension state*, *shared struct*.

This spec is not a pure refactor: public interfaces of Iris and FenrirScripting change, and tests change with them. The public surface of the `Fenrir.Physics` contract assembly does not.

## Problem statement

**E.** A physics component field is written in about six places: the C++ struct (name, type, default, doc comment), its `Iris::StructMapping` (`IRIS_FIELD_AS`: name again, with its C# spelling), a stand-in struct when the real type has private members or an `Entity`, the C# struct (name, type, default, doc comment, and explicit `FieldOffset`s for a tagged union), a hand-written defaults test, and the tests' twelve `Same()` copies. `packages/FenrirPhysics/Scripting/src/PhysicsScripting.cpp` holds twenty `Detail::*Layout` stand-ins: three for the private tag and union of `CollisionShape`, `JointKind` and `VehicleKind`; three more for the components holding them; six for components and five for events that hold an `Entity` (a `u32` and a private `EntityList*`, which Iris cannot map); three for empty structs (`FixedJoint`, `BallJoint`, `TrailerVehicle`), which Iris refuses to map, each with a private `_unused` byte on the C# side. `MakeComponent` checks only that a stand-in has the size and alignment of the real type. Defaults are tested for some types and not for others (none for `PhysicsMaterial`, `CollisionFilter`, `Trigger`, `ContactEvents` or the shapes). The query structs natives take by pointer (`RaycastHit`, `QueryFilter`, `ShapeCastHit`, ...) are never checked against anything: a drifted one corrupts memory silently.

**H.** The extension reaches its package through a back door. `FenrirPhysicsScripting` includes FenrirPhysics's private headers (`PRIVATE ../src` for `BodyComponents.hpp` and `PhysicsState.hpp`). Three process globals exist for it: `scriptedOwner`/`ownerMutex` (set as a side effect of the `const` `Physics::ScriptingExtension()`, last owner wins, used only by the two collision-mesh natives), a debug `warned` set, and `lastInstalled`/`WarnThroughInstalledApp` in FenrirPhysics itself, used only so `FrameFromWorld` can warn with no app in hand. The 23 natives are each spelled three times (the C++ function, its `NativeFunction::Make<&F>(prefix + "Name")` registration, the C# `[NativeFunction] partial` declaration), three of them under names that differ from the C++ function's; 17 guard themselves with `WorldFor(context, "Name")`, only two zero their outputs before an early return, and `FrameFromWorldNative` checks for no world at all. FenrirScripting's own built-in extension is no better: 24 hand-registered names and a private `CallContextAccess` channel to its `EngineContext`, which `CallContext` carries as a field for no other extension's benefit. `Physics.hpp` declares `ScriptingExtension()` for a library FenrirPhysics never links, and documents it against the wrong C# class.

What the research showed: nobody generates structs. s&box's InteropGen describes *functions* once and generates both sides, but a struct is a name pair hand-written on both sides with only `sizeof` compared at start-up; Coral and Mono check nothing. Iris's load-time check (size, field names, offsets, kinds) is already the strictest of the four. The gap is duplication, not safety. s&box's two lessons that do carry over: generate both sides of a function table from one ordered model so they cannot drift, and keep bindings out of the collectible load context (which the shared contract assemblies already do).

## Solution

**The C++ header is the one description.** Nothing about a shared type is written a second time by hand. Two generated stages stand between a header and C#, with a committed, language-neutral file between them, so the engine stays binding-agnostic (Godot's `extension_api.json` is the model).

### Stage 1: the API description (engine tool, binding-agnostic)

- `tools/describe-api/` is a .NET console tool using ClangSharp as a libclang header reader. Its output is neutral JSON; its own language is nobody's concern. It knows nothing of Iris or FenrirScripting.
- Each package that shares types has `packages/<Package>/api/`: a hand-written **selection** (`<Package>.api.toml`; the exact format is a build-ticket detail) naming the headers to read, the types to share and each one's role (component, event, shared struct, or bound type), and the generated **API description** `<Package>.api.json`, committed.
- The description records, per type: its C++ name, size, alignment and role; each field's name, byte offset, kind (number, bool, char16, enum, struct, array, pointer-sized opaque), element kind and length for arrays, default value and doc comment; each enum's underlying type, values and docs; each tagged union's tag field and the kinds at the union's offset. A bound type is recorded by layout only. The file records `"pointerSize": 8` and the alignment model it was read for; the tool refuses any other target.
- Defaults are the C++ default member initialisers, recorded as values where the expression is a literal, an enum value, a known constructor of a bound type (`Math::Vec3(0.5f)`, `Math::Vec3(0, 1, 0)`, an identity quaternion), `std::numeric_limits<float>::infinity()`, or a nested type's own defaults (`T{}`). Any other expression fails the tool naming the field. Stage 2's generated test (below) is the second guard: a wrong value can never ship.
- Doc comments: the `@brief` text and the comment body are copied as text. No cross-references are rewritten.
- A **tagged union** is recognised by shape: a class whose non-static data members are one enum tag followed by one anonymous union. `CollisionShape`, `JointKind` and `VehicleKind` have it. Any other class with private members, a virtual function, a non-trivial member or a member Iris cannot map fails the tool with a diagnostic naming the type and member; nothing is guessed at and no stand-in is made.
- Running: a CMake target not in `ALL` (`fenrir-describe-api`) regenerates every package's description; the ordinary build never runs the tool. CI runs the target and fails on a dirty tree.

### Stage 2: the C# binding (Iris tool)

- `packages/Iris/Tools/<name>` (name chosen in its ticket) reads one or more API descriptions plus the binding's own configuration and writes committed source. The configuration lives with the binding (`packages/<Package>/Scripting/api/`): the C# namespace, which C# type stands for each bound type (`Math::Vec3` → `System.Numerics.Vector3`, `Math::Quat` → `Quaternion`, `Fenrir::Transform` → `Fenrir.Scripting.Transform`), and the exceptions to the PascalCase rule (`hull` → `ConvexHull`).
- It emits, for each description:
  - **C# mirrors** as `partial` structs into `Managed/<Contract>/Generated/`: fields in order with PascalCase names, field initialisers from the recorded defaults and an explicit `public T() {}`, `<summary>` docs from the C++ comments, `[NativeComponent("Namespace.Type")]` for a component, the event attribute for an event, and for a tagged union the storage the existing `[TaggedUnion]` Roslyn generator consumes (the tag field, `[StructLayout(LayoutKind.Explicit)]`, one `[FieldOffset]` field per kind). Nothing else changes in the SDK's generators: `ComponentFieldsGenerator`, `TaggedUnionGenerator`, `EventGenerator` and `NativeFunctionGenerator` keep consuming what they consume today.
  - **Iris mappings** into `Scripting/src/Generated/`: one `Iris::StructMapping<T>` specialisation per shared type, each field declared by kind and offset with the new `Iris::Field<Kind>("Name", offset)` (no member access, so private members and `Entity` need no stand-in), with `static_assert(sizeof(T) == N && alignof(T) == A)` beside each.
  - The extension's **registration lists**: `components`, `events` and `sharedStructs`, each `Make...<T>("Namespace.Type")` with the C# name derived from namespace and type, so `MakeComponent<RigidBody>("Fenrir.Physics.RigidBody")` is never written by hand.
  - A **generated test** per shared type and event, built into the extension's test target: a C# `new T()` written into engine memory equals C++'s `T{}` field by field through the mapping (padding excluded, so no byte compare), so the hand-written defaults tests and the tests' `Same()` copies go.
- Hand-written C# beside a generated mirror is a second `partial` part that adds behaviour only: `Collider(CollisionShape)`, `IEquatable<CollisionMesh>`, a tagged union's `Noun`, the `PhysicsWorld` wrappers. A hand-written field in a mirrored struct is a generator error.
- Running: a CMake target not in `ALL` (`fenrir-generate-bindings`); CI regenerates and fails on a dirty tree.

### Native functions spelled once

The 23 physics natives and the built-in extension's 24 are glue specific to the C# binding, so they are not in the neutral description (another binding writes its own against the package's public C++ API). But within the C# binding they are also spelled once: `tools/describe-api` has a second mode that reads an extension's C++ natives, recognised by their first parameters (`const CallContext&`, then optionally the extension's state), into a binding-side file (`Scripting/api/<Package>.natives.json`), and the Iris tool emits from it the C# `[NativeFunction] static partial` declarations (dropping the leading context and state parameters, mapping `const Vec3*` to `in Vector3`, `T*` outputs to `out T`, `u32 entity` to `Entity`, as the hand-written declarations do today) and the C++ registration list (`extension.functions = {...}`, in one order from one model). A native's name is its C++ function name; `CreateMesh`, `ReleaseMesh` and `FrameFromWorldNative` are renamed to the names C# calls them by.

### The extension seam, without the back door

- **Extension state** (ADR-0009 amended): `ScriptingExtension` gains a typed state (held as `std::shared_ptr<void>` with its `std::type_index`, set through a typed setter). A native function may take it as its second parameter: `R F(const CallContext&, State&, Args...)`; a stateless native keeps `R F(const CallContext&, Args...)`. `NativeFunction::Make<&F>` records the state type it expects; `Install` fails with a diagnostic naming the function when an extension does not carry that type. The trampoline Iris calls finds the state through the per-function slot `Install` fills, beside today's `notRunning` value.
- **The built-in extension** uses it: `EngineContext` becomes its state, `CallContext` loses `m_engine`, `Detail::CallContextAccess` and `Detail::ActiveCallContext`'s engine side go. The `activeContext` atomic remains only as the one "is a Scripting running" switch that every native shares.
- **Shared structs**: `ScriptingExtension::sharedStructs` lists the plain data natives take or return by pointer; `Install`'s script-load check validates them against their mappings exactly as it does components and events (`ScriptSchema.cpp`).
- **Physics reaches no owner.** The only natives that needed the owner are the two collision-mesh ones; `PhysicsWorld::Impl` already holds the owner's mesh store. So `PhysicsWorld` gains `CreateCollisionMesh`/`ReleaseCollisionMesh` forwarding to it (useful to any C++ system holding a world), the mesh natives go through the active scene's world like every other native, and a script with no active physics world cannot make a mesh (documented in `PhysicsWorld`'s remarks, warned once). The physics extension's state is then only its debug `warned` set. Gone: `scriptedOwner`, `ownerMutex`, `OwnerFor`, the process-wide `warned` set, `PRIVATE ../src` (`HasBodyComponent` is rewritten in the extension from public components), `Physics::ScriptingExtension()` and its declaration and forward declaration in `Physics.hpp`. The extension is made by a free function in its own header, `Fenrir::PhysicsScriptingExtension()`, taking nothing.
- **One helper gives each native its world.** `physics.WorldOf(context)` (a method on the state) returns the active scene's world or null, warning once per call site in debug through the call context's app. Every native with an `out` parameter zeroes it before any early return; the generated natives test exercises that for each one.
- **`FrameFromWorld`**: the native warns through its call context when a body is dead; the C++ free function documents that a dead body yields identity frames and does not warn; `lastInstalled` and `WarnThroughInstalledApp` go from FenrirPhysics.

### Iris changes (ADR-0006 amended: 0.3 may break, each break logged, re-freeze at 1.0)

- `Iris::Field<T>(name, offset)`: declare a mapped field by kind and byte offset without member access. `IRIS_FIELD`/`IRIS_FIELD_AS` stay for hand-written mappings.
- A **pointer-sized opaque** field kind (`ValueKind::Pointer` as a struct field): the managed field is `nint`/`nuint`/`ulong`-sized at the same offset, its contents never interpreted. `Fenrir::Entity` then maps directly to the existing C# `EntityLink { uint Value; uint Reserved; ulong Owner; }`, and `Detail::EntityLinkView` goes.
- **Zero-field structs**: a mapping with no fields is accepted when both sizes are 1 (an empty C# struct is one byte, as an empty C++ struct is), so `FixedJoint`, `BallJoint` and `TrailerVehicle` map as themselves and lose their private `_unused` field.
- The generator tool, as an Iris tool with its own tests against a fixture description, knowing nothing of Fenrir.

### Scope

Both extensions, physics first in the stack: FenrirPhysics (components, events, shared structs, 23 natives) and the built-in extension (FenrirECS's `Name` and `Relationship`, FenrirMath's `Transform`, the `Entity`/`EntityLink` link type, `Vec3` and `Quat` as bound types, 24 natives). FenrirPhysics, FenrirECS and FenrirMath gain `api/` folders; `EngineMappings.hpp`'s hand mappings and `Bindings.cpp`'s hand-registered list go.

### Out of scope

- `[SuppressGCTransition]` on short natives: a later additive Iris option, not measured as needed.
- A whole-contract hash at load (s&box's `igen_*` hash): Iris's per-field check is stricter and names the field.
- Per-platform descriptions: one target (64-bit, 8-byte pointers); Iris's load-time check guards a build that disagrees.
- Generating `Same()` in FenrirPhysics: the engine's overloads encode diff rules (what is live, what rebuilds) and stay hand-written; only the tests' copies go.
- Candidate F (scene-scoped events).

## What does not change

- `Fenrir.Physics`'s public surface: the same public names, order, offsets and defaults, verified once by diffing the generated assembly's public API against today's (ticket 07). The only visible difference is the three private `_unused` fields vanishing.
- Iris's per-field load-time check stays the gate. A component is still registered before the script assembly loads and checked as it does.
- `Ref<T>`, queries, events, the tagged-union views and the `NativeFunction` binding-name rule (`Namespace.Type.__native_Method`).
- FenrirPhysics never links FenrirScripting or Iris; it now names neither in any header.

## Acceptance

- No `Detail::*Layout` or `EntityLinkView` stand-in remains; `grep -r "Layout\b" packages/FenrirPhysics/Scripting/src` finds none.
- Every shared type of both extensions is described in a committed `api/*.api.json`, and `fenrir-describe-api` followed by `fenrir-generate-bindings` leaves `git status` clean; CI enforces it.
- The generated defaults test covers every component, event and shared struct of both extensions and passes; the hand-written defaults tests and the tests' `Same()` copies are deleted.
- `grep -rn "scriptedOwner\|ownerMutex\|lastInstalled\|WarnThroughInstalledApp\|CallContextAccess\|PRIVATE ../src" packages` finds nothing.
- Each native function's name appears once in C++ (its definition); the C# declaration and the registration are generated. The `FenrirPhysicsScriptingTests` and FenrirScripting's tests pass, as does the DemoApp's physics smoke run.
- `Fenrir.Physics`'s public API diff against the pre-spec assembly shows only the removed `_unused` fields.

## Tickets

Stacked in this order (each branch cut from the previous ticket's pushed branch, each PR targeting it), because the Touches lines chain through `packages/Iris`, `packages/FenrirScripting` and `packages/FenrirPhysics/Scripting`:

| # | Ticket | Area |
|---|--------|------|
| 01 | Iris 0.3: fields by offset, opaque pointers, empty structs | `packages/Iris` |
| 02 | Extension state and shared structs in FenrirScripting | `packages/FenrirScripting` |
| 03 | Close the physics extension's back door | `packages/FenrirPhysics`, its `Scripting/` |
| 04 | `tools/describe-api`: headers to an API description | `tools/`, root CMake, CI |
| 05 | FenrirPhysics's API description | `packages/FenrirPhysics/api/` |
| 06 | Iris's binding generator | `packages/Iris/Tools/` |
| 07 | Physics components, events and shared structs generated | `packages/FenrirPhysics/Managed`, `Scripting/` |
| 08 | Physics natives spelled once | `tools/describe-api`, `packages/Iris/Tools/`, `Scripting/` |
| 09 | The built-in extension on the same tooling | `packages/FenrirECS/api`, `packages/FenrirMath/api`, `packages/FenrirScripting` |

04 touches nothing 01–03 touch and may be built side by side off the base, with 05 after it; the stack order above is still the merge order. Each ticket is built in its own session (`docs/agents/issue-tracker.md`).
