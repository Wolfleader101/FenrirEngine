---
status: accepted
---

# Scripting extensions carry native resources and deferred structural changes

ADR-0009 gave a scripting extension three things: native functions, native components with C++ events, and its shared assemblies. The renderer's packages need more than that. Much of what they offer C# is a resource, not a component: `Sky` and `AmbientLight` are scene resources, `InputClaims` and the input devices are app resources. Spawning a model from C# also creates entities, which a native function may not do. Physics could avoid both: its one resource-like setting, gravity, became a get and set pair of native functions. That works for one value, but each new resource would add two more functions, and the C# API would differ from C++ for no gain. This ADR fixes both gaps in ADR-0009's mechanism, so that every package uses it the same way.

## Native resources

- **A plain-data resource crosses as a mirror, as a component does.** The extension registers it with `MakeSceneResource<T>("Fenrir.Renderer.Sky")` or `MakeAppResource<T>(…)` (with read-only variants). C# declares a blittable struct with `[NativeResource("…")]` under the same name. The layout is checked when scripts load, as for components, and a mismatch fails the load. *Amended by ADR-0043:* the registration and the C# struct are generated. The package's API selection gives the type the role `scene-resource` or `app-resource`, the binding's `read-only` list says whether C# may write it, and the mirror, mapping, registration and defaults test follow from the description. A resource C# cannot read in place (`InputClaims`, whose windows are a vector) crosses as a binding-owned plain-data copy, described from the binding's own selection, whose refreshing registration is the one part written by hand.
- **C# reaches it with `Resources.Get<T>()`.** This returns a `Ref<T>`, or a `ReadOnlyRef<T>` for a read-only resource, into the engine's storage. `Resources.TryGet<T>(out …)` is the form for a resource that may be absent. A scene resource is the active scene's. Access works from any C# code, not only from systems, so the system generator stays as it is.
- **Inserting or removing a resource is a structural change.** It is recorded and played back like adding a component, so a `Ref<T>` taken before it stays valid.
- **A resource with behaviour stays a facade of native functions.** `Keyboard`, `Mouse`, `Gamepads` and `DebugDraw` are C++ classes whose answers depend on the calling phase or on calls: input's edges follow the phase (ADR-0017), and debug draw tessellates (ADR-0025). C# gets static classes with the same names whose functions ask C++, so C# never re-implements those rules. The rule is: **a resource whose data is its API is mirrored, and a resource with methods is a facade.** *Amended by ADR-0043:* the facade is written over generated natives: the `<Package>Native` class and its registration come from the C++ natives (typed, since ADR-0044), and the facade holds only what a description cannot say: overloads, generic methods, properties over outputs, constants, and a real conversion. A facade is a hand-written part over a generated part: the generated part holds the natives whose public shape is the declaration's, each named by its `@facade`, and the hand-written part what a description cannot say.

## Deferred structural changes for extensions

- **The call context gives an extension the change buffer** that the engine's own entity functions use. ADR-0009's "possible later addition" lands here. An extension's native function may create entities, destroy them, add or remove components, and insert or remove resources through it. A created entity is alive and usable at once. Everything else plays back, in order, when the native code that called the script gets control back.
- **Direct structural changes stay forbidden.** The rule ADR-0009 set for the calling system's `Ref<T>`s is unchanged: a native function never touches the storage structurally, it records.
- The first user is the renderer's `SpawnModel`, which builds a model's entity tree and returns its root.
- *Amended by ADR-0039:* a C# system that runs inside an extension stage records into the same buffer, but the buffer plays back when the extension's hosting work returns (`StageChanges::PlayBack`), not when the system returns, so the C++ work and the stage systems after it never see entities or components appear or vanish halfway through. A stage system that throws has only what it recorded dropped (the runner marks the buffer before each one), and is disabled until the next reload.

## Engine math is its own contract

- **`Fenrir.Math`** is a pure managed shared assembly in `packages/FenrirMath/Managed/Fenrir.Math`, in namespace `Fenrir.Math`, with no native functions. It holds `Vector2`, `Vector3`, `Vector4`, `Quaternion`, `Matrix4x4`, `Colour`, `Ray` and `Bounds`, each laid out as its FenrirMath counterpart.
- `Vector3` and `Quaternion` move there from `Fenrir.Scripting`. `Transform` stays in `Fenrir.Scripting`, because it is an engine component. The built-in extension shares `Fenrir.Math` first, before `Fenrir.Scripting`, so every package's contract can use it without depending on another package.
- *Since ADR-0042:* FenrirMath's API description records its vector and quaternion as bound types, and the generated bindings (the built-in extension's and physics's) name `Fenrir.Math`'s `Vector3` and `Quaternion` for them. The other math types are mapped by hand, beside those two, in FenrirScripting's `BoundMappings.hpp`, until FenrirMath's description covers them. `Math::AABB` is mapped as FenrirPhysics's `Aabb` by physics's binding, so `Bounds` maps a stand-in of the same layout; whether physics's `Aabb` becomes `Fenrir.Math`'s `Bounds` is open. *Settled by ADR-0043:* `Colour`, `Ray` and `Bounds` are generated mirrors, from FenrirMath's description through a math binding, with their operators as hand-written parts; `Vector2`, `Vector3`, `Vector4`, `Quaternion` and `Matrix4x4` stay bound types, hand-written and hand-mapped, because glm's layout cannot be described. `Math::AABB` has the one C# type `Bounds`; physics's `Aabb` and the stand-in go.

## Considered options

- **Native get and set functions per resource** (physics's gravity). Rejected: it adds two functions per resource, a C# API that differs from C++, and public names that would change once mirrors arrived.
- **Mirroring every resource, input devices included.** Rejected: C# would need both edge sets and the phase rule, and would drift from C++.
- **Resources as system parameters** (`Res<T>`/`ResMut<T>`, as Bevy does). Rejected for now: the system generator grows, and helper methods outside a system could not reach resources. It can be added over `Resources.Get` later.
- **Keeping ADR-0009's ban, so no `SpawnModel` from C#.** Rejected: scripts would spawn models by asking C++ or by rebuilding the tree by hand.
- **Math kept in `Fenrir.Scripting`.** Rejected: math belongs to FenrirMath, which every package depends on, and not to scripting.

## Consequences

- ADR-0009 is amended. An extension also carries native resources, and its native functions may make deferred structural changes.
- FenrirScripting gains a prerequisite in the foundation spec: native resources (`[NativeResource]`, registration, layout checks, `Resources`, deferred insert and remove), the change buffer on the call context, and the `Fenrir.Math` move. The README's extension sections are rewritten to cover it.
- Physics's `PhysicsWorld.Gravity` is a candidate to become a resource mirror. That is the physics map's call.
- Resources defined in C# by script assemblies (a game's own singletons) are not part of this. They are left for later.
- *Amended by ADR-0039:* an extension may also declare stages that C# systems join; FenrirSkeleton's pose stages are the first.
- *Amended by ADR-0043:* every shape this ADR introduced (resources, the copy for one that cannot be read in place, facades, `Fenrir.Math`) is expressed in ADR-0042's generated pipeline, and the five extensions built by hand before ADR-0042 are ported onto it.
