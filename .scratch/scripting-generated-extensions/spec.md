Status: ready-for-agent

# Scripting: the hand-written C# extensions move onto the generated API pipeline

Origin: a grilling session over the five hand-written scripting extensions (`Fenrir.Input`, `Fenrir.Window`, `Fenrir.Assets`, `Fenrir.Renderer`, `Fenrir.DebugDraw`) and the `Fenrir.Math` assembly, built by the FenrirRenderer foundation spec's tickets 21–26 before ADR-0042 made C# contracts generated. The hard-to-reverse calls are ADRs:

| ADR | Decision |
|---|---|
| 0043 | Native resources and facades move onto the generated API pipeline (amends 0029) |
| 0044 | Natives pass enums, text and wrappers by value, and bindings use other bindings' mirrors (amends 0042) |

## Problem Statement

`dotnet-scripting` holds two ways of giving C# a package. FenrirECS, FenrirMath, FenrirPhysics and FenrirScripting commit an API selection and description (`api/<Pkg>.api.toml`, `.api.json`), a binding configuration (`<Pkg>.binding.toml`) and a natives description (`.natives.json`), from which Iris.Bindings generates the C# mirrors, the Iris mappings, the registration lists and the native declarations. The five renderer-stack extensions and `Fenrir.Math` spell every one of those by hand: a C# struct and an `Iris::StructMapping` per type, a `[NativeFunction]` declaration and a `NativeFunction::Make` per native, an events list, a resources list. Each place can drift from its C++ counterpart, and only Iris's load-time check stands between a drift and corrupted memory, which is the state ADR-0042 ended for physics.

The generator could not take them as they were. It had no resource roles; it refused `std::string_view`, `char32_t`, enums and id structs by value, so the facades carried casts and the natives took integers; it mirrored every type of every description it read, so a binding holding another package's type would mirror it twice; and `Math::AABB` had two C# types because one C++ type has one Iris mapping.

## Solution

Two generator tickets, then five ports:

- **The runtime boundary** (Iris and the SDK's source generator) learns enums by value and one-field wrappers.
- **The generators** (describe-api and Iris.Bindings) learn text, `char32_t`, the resource roles, internal mirrors, hand-written registrations and bindings that use other bindings' mirrors.
- **`Fenrir.Math`** becomes three generated mirrors (`Colour`, `Ray`, `Bounds`) beside five hand-written bound types, through a math binding; physics's `Aabb` becomes `Bounds`.
- **Input and Window, Assets, Renderer and DebugDraw** each become a selection, a binding configuration and generated outputs beside a hand-written facade and extension state. Each port keeps the public C# API as it is, except `Aabb`.

## User Stories

### An engine developer adding to a ported package

- I add a field to `Camera` in `Components.hpp`, regenerate, and review the diff of the mirror, the mapping and the defaults test. I write no C#.
- I add a native to `RendererScripting.cpp` taking a `WindowId` and a `CursorShape`, regenerate, and the generated `RendererNative` declares `WindowId` and `CursorShape`. I add one line to the facade if the facade should offer it.
- I add a scene resource to FenrirRenderer, give it the `scene-resource` role, regenerate, and C# reaches it with `Resources.Get<T>()`.

### A game developer

- `Keyboard.JustPressed(Key.Space)`, `Window.Size(Window.Main)`, `Assets.Load<Mesh>("…")`, `DebugDraw.Line(…)` and `Resources.Get<Sky>()` work as before.
- `Aabb` in my physics code is now `Bounds`, from `Fenrir.Math`, which `Renderer.GetWorldBounds` and `DebugDraw.Box` already took.

### A reviewer

- No `IRIS_FIELD` for a described type, no hand-written `[NativeFunction]` declaration, and no `NativeFunction::Make` list remains in the five extensions. The glossary's note that they declare natives by hand is gone.

## Implementation Decisions

### The runtime boundary (ADR-0044, ticket 01)

- **Iris, enums.** `InternalCallParameterType` and `InternalCallResultType` accept an enum; `WireOf<E>` wires it as `std::underlying_type_t<E>` and casts; `InternalKindOf<E>` is the underlying kind. The managed bridge is unchanged: the field's parameter is the integer.
- **Iris, wrappers.** `Iris::WrapperOf<T>` names a one-field struct's field type (`template <> struct Iris::WrapperOf<Fenrir::WindowId> { using Value = std::uint32_t; static Fenrir::WindowId From(Value); static Value To(Fenrir::WindowId); }`, or an equivalent the ticket settles); `WireOf<T>` for a type with a trait wires the field. `InternalCallParameterType` and `InternalCallResultType` accept it. Iris's own tests cover both over its fixture.
- **SDK NativeFunctionGenerator.** `PassesThrough` accepts an enum (field type the underlying type, argument cast) and a one-field unmanaged struct (field type the member's pass-through type, argument the member), as parameter and result. The `Entity` special case is deleted: `Entity` is a wrapper of its `Value` auto-property. Compile-fail fixtures cover a two-field struct and a struct whose field is itself refused.

### The generators (ADR-0043, ADR-0044, ticket 02)

- **describe-api.** A `std::string_view` parameter is kind `string` (spelled `std::string_view`); `char32_t` is `u32` in fields and parameters; the selection roles `scene-resource` and `app-resource` are recorded in the description's `role`. `SCHEMA.md` and `docs/api-description.md`/`natives-description.md` say so. The fixture gains a resource of each scope and a native taking text and `char32_t`.
- **Iris.Bindings, natives.** Kind `string` declares `ReadOnlySpan<char>` over the wire pair `void*, int`; an `enum` parameter or result declares the described enum (wire its underlying type); a `struct` parameter or result whose described type has one field, or is a bound type the configuration marks `wrapper = "<field>"`, declares the binding's type for it (wire the field's kind). Anything else by value still fails.
- **Iris.Bindings, resources.** A `scene-resource` or `app-resource` mirror carries `[<resource-attribute>("Namespace.Type")]` and `<resource-interface>` (or the read-only interface when in `read-only`), a union mirror included; `[registration]` gains `make-scene-resource`, `make-read-only-scene-resource`, `make-app-resource`, `make-read-only-app-resource`, and the generated function sets `extension.resources`; the defaults test gets a case per resource. `read-only` entries may name resources.
- **Iris.Bindings, `internal-structs`.** Listed described structs are mirrored `internal`.
- **Iris.Bindings, `by-hand`.** Listed types get a mirror, mapping and defaults case but no `Make…` line in the generated registration.
- **Iris.Bindings, `[uses]`.** A list of other binding configurations (paths). A type described in a description one of them binds, and not excluded there, is used by its C# name in that binding's namespace, with that binding's mappings header included, and is not generated here. Every binding listing the math binding names `Colour`, `Ray` and `Bounds` that way; `[bound-types]` still names the glm five.
- The fixture binding exercises each; `Fixture.natives.json` is the copy of describe-api's.

### `Fenrir.Math` (ADR-0043, ticket 03)

- `FenrirMath.api.toml` adds `Fenrir::Colour`, `Fenrir::Math::Ray` and `Fenrir::Math::AABB` as `shared-struct`, and `Fenrir::Math::Vec2`, `Vec4`, `Mat4` as `bound-type` beside `Vec3` and `Quat`.
- `packages/FenrirScripting/api/FenrirMath.binding.toml`: namespace `Fenrir.Math`, contract `packages/FenrirMath/Managed/Fenrir.Math`, generated `…/Generated`, mappings into `packages/FenrirScripting/include/FenrirScripting/Generated/FenrirMathMappings.hpp`, `[names] "Fenrir::Math::AABB" = "Bounds"`, `"Fenrir::Math::Ray::dir" = "Direction"`, no registration (the built-in extension shares the assembly). The hand-written `Colour.cs`, `Ray.cs` and `Bounds.cs` keep only behaviour as partial parts. `BoundMappings.hpp` keeps the glm five, with `Mat4View` replaced by a mapping that gives the four column offsets without naming members, and loses `Colour`, `Ray` and `BoundsView`.
- Physics: `FenrirPhysics.binding.toml` adds `[uses]` with the math binding, drops `[names] "Fenrir::Math::AABB" = "Aabb"`, `Generated/Aabb.cs` is deleted by the generator, and every `Aabb` in `Fenrir.Physics`, its fixture and the demo scripts becomes `Bounds`. The built-in binding adds `[uses]` too.

### Each port (ADR-0043, ADR-0044, tickets 04–07)

The shape every port follows; each ticket names its own types:

- **Selection.** `packages/<Pkg>/api/<Pkg>.api.toml` selects the package's headers and gives each component, event, resource and shared struct its role; a binding-owned `packages/<Pkg>/Scripting/api/<Pkg>Scripting.api.toml` selects the plain-data copies the natives take in C#'s form (`ScriptInputClaims`, `ScriptShapeOptions`, `ScriptTextOptions`, `ScriptShaderParameter`).
- **Binding.** `packages/<Pkg>/Scripting/api/<Pkg>.binding.toml`, after physics's: `[uses]` the bindings it holds types of, `[natives] sources` the natives file, `[registration]` into `src/Generated/<Pkg>Registration.cpp`, `[tests]` the defaults and natives tests into the extension's test target.
- **Natives.** Each native is rewritten to take what the C++ API takes: `std::string_view` for text, the enum, the id struct (with `Iris::WrapperOf<T>` specialised beside the mapping); `@param[out]` on outputs; a doc comment that becomes the C# declaration's. The extension's warn-once record moves to a state class the natives take second (`SetState`), and the per-process record goes.
- **Facade.** The public static class is kept, rewritten over the generated `<Pkg>Native`: casts and `.Value` unwrapping deleted; everything else as it was. Its remarks keep the no-op rules.
- **Mappings and declarations deleted.** `<Pkg>Mappings.hpp`'s `IRIS_FIELD` mappings for described types go (a package another binding uses keeps nothing: the generated mappings header is what `[uses]` includes); every hand-written mirror and `[NativeFunction]` declaration goes; `extension.functions`, `.events`, `.resources`, `.components` lists are replaced by `Generated::Register<Pkg>(extension)` plus any `by-hand` registration.
- **Bound types mapped by hand.** `AssetHandle<Mesh>` and the other four are `bound-type`s of FenrirAssets' description (describe-api instantiates a never-instantiated specialisation to read its layout), named in `[bound-types]` as `AssetHandle<Mesh>` and so on, and mapped by hand with the full generic .NET type name as today. A handle crosses a native as the packed `std::uint64_t` it does now.
- **Tests.** The existing extension tests stay and pass; hand-written layout tests are replaced by the generated defaults test; the enum-value tests stay (an enum's values are described, so a mismatch is now a generator diff, but the test is cheap).

### Order and parallelism

01 → 02 → 03 → {04, 05, 07} → 06. The generator tickets stack (02 off 01). 04, 05 and 07 run side by side off 03 (disjoint Touches; 07 uses only Math's mirrors). 06 waits for 04 and 05, whose mirrors it uses. Whichever port lands last closes the spec (the glossary note and this directory): 07's ticket carries that step, and the one merging it moves it if 07 is not last.

## Testing Decisions

- Iris: an enum and a wrapper cross an internal call in both directions in Iris's own tests; a two-field struct by value is refused at compile time.
- SDK: compile-fail fixtures for a two-field struct and a nested refusal; the harness calls a native taking an enum and `WindowId`-like wrapper.
- describe-api and Iris.Bindings: fixtures hold every new form; the committed fixture outputs are the oracle; each new diagnostic has a case.
- Each port: its existing extension tests (fed input, hidden-window clipboard, materials, picking, debug-draw items, no-op rules) pass unchanged in behaviour; the generated defaults and natives tests run in its test target; `fenrir-describe-api` and `fenrir-generate-bindings` leave the tree clean, which CI checks.
- Math: physics's scripting tests and the C# demo scripts build with `Bounds`.
- Build rule for a session doing these tickets: `msvc-x64-debug` only, and only the tests the ticket touches; CI covers the rest.

## Out of Scope

- Full struct-by-value in Iris (ADR-0044 leaves it open).
- Generated facades.
- Describing glm's types field by field.
- Resources defined in C# by script assemblies (ADR-0029 leaves them for later).
- `PhysicsWorld.Gravity` as a resource mirror (the physics map's call).
- Any change to the engine's C++ types for a binding's sake.

## Further Notes

- The old spec's tickets 21–26 are in history: `git show 28d8f69:.scratch/fenrir-renderer-foundation/spec.md` and `.../issues/`.
- When the last ticket merges, the commit that records it deletes this directory and removes the glossary's "still declare both by hand" note under *Native function*.
