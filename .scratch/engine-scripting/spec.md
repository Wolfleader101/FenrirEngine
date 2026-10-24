# Spec: C# scripting in FenrirEngine

**Status:** ready-for-agent

## Goal

Let game code be written in C# against the same ECS the C++ side uses: C# components are blittable structs stored in EnTT, C# systems run in the engine's frame phases, and a rebuilt script assembly hot-reloads without losing component data. Everything is built on the frozen Iris 0.2 API (ADR-0006). Anything Iris is missing becomes an additive Iris ticket in `.scratch/dotnet-scripting/issues/`, not an engine workaround.

The architecture is recorded in ADR-0007, and the vocabulary in `GLOSSARY.md` under "FenrirEngine scripting".

## What a script author sees

```csharp
// In a script assembly, compiled against Fenrir.Scripting.
public struct Velocity : IComponent { public Vector3 Linear; }
public struct Damage : IEvent { public Entity Target; public float Amount; }

[System(Phase.Update, Order = 10)]
public sealed class Mover : ISystem
{
    public void OnUpdate(in SystemContext ctx)
    {
        foreach (var (entity, transform, velocity) in ctx.Query<Transform, Velocity>())
            transform.Position += velocity.Linear * ctx.Time.DeltaTime;   // writes native memory

        foreach (var hit in ctx.Events.Read<Damage>())
            ctx.Log.Info($"{hit.Target} took {hit.Amount}");
    }
}
```

`var t = entity.Get<Transform>(); t.Position.X += 1;` also writes straight through to EnTT.

## Decisions

### Model

- **ECS-first** (ADR-0007). There are no per-entity behaviour objects in this spec.
- **Components** are `unmanaged` structs implementing `IComponent`. A C# component is stored in EnTT in an engine-provided byte pool, one per type, named by the type's full name (EnTT 3.13 has no pool whose type is known only at runtime). Native components are exposed to C# as `IComponent` structs too, so queries mix them freely.
- **Systems** are classes implementing `ISystem`: `OnUpdate(in SystemContext)`, plus optional `OnCreate` and `OnDestroy`, and `[System(Phase, Order)]`. The phases are `PreUpdate`, `Tick`, `Update` and `PostUpdate`, mapped to `SchedulePriority`.
- **Entity** is `readonly struct Entity { uint Value; }`, the full `entt::entity` value including version bits, with helper methods. Every operation works on the active scene. There is no cross-scene API in v1.
- **Events** are `unmanaged` structs implementing `IEvent`, with the same double-buffered semantics as `App::SendEvent`/`ReadEvents`. A C#-only event needs no C++.

### Access

- **`Ref<T>`** is a `ref struct` wrapping a pointer into EnTT storage. A source generator in the SDK emits a C# 14 extension property for each field (`ref Vector3 Position`), so writes need no `ref` at the call site. *Confirmed by the ticket 03 spike:* a C# 14 `extension(Ref<T> self)` property can return `ref self.Value.Field` on a `ref struct` receiver, and `t.Position.X += 1` writes the native memory. The `t.Value.Position.X` fallback was not needed (it still works). The SDK is a folder with the contract, the generator under `analyzers/` and `Fenrir.Scripting.props`, which a script project imports. The properties cover each public or internal instance field (not static, `const`, `readonly` or fixed-buffer ones); the generator refuses, with a diagnostic, a component that is generic, not unmanaged, a `readonly struct` or nested in a private type. The compiler refuses a `Ref<T>` in a field, a lambda capture, a collection, an async parameter or a local held across an `await` (compile-fail tests).
- **Queries**: `foreach (var (e, a, b) in ctx.Query<A, B>())` over a `ref struct` row with a generated `Deconstruct`, plus `Query<A, B>().ForEach((Entity e, Ref<A> a, Ref<B> b) => ...)`, which mirrors native `ForEach`. A query costs one native-to-managed transition: native code builds the view and fills a scratch buffer of entity plus component pointers. Generated arities go up to about 8. All access is read-write in v1.
- **Structural changes** (create, destroy, add, remove) and event sends from a C# system are recorded and applied when that system returns. A `Ref<T>` therefore stays valid for the whole system. A component added during a system is visible from the next system on.

### Binding native code

- Exposing a C++ type takes two pieces of glue: `scripting.RegisterComponent<T>("Ns.Name")` (or `RegisterEvent<T>`) with an Iris `StructMapping`, and a hand-written matching C# struct. The layout is validated on load (Iris ticket 21), and a mismatch fails the load.
- Functions go through Iris `RegisterInternalCall` with a static C# wrapper in the contract. The engine's own API uses the same pattern, which covers review-18 item E4.
- Generating the C# side from C++ is future work.

### Loading, scheduling and threading

- **Contract**: the shared assembly `Fenrir.Scripting` holds `Entity`, `IComponent`, `IEvent`, `ISystem`, `SystemContext`, `Ref<T>`, the query types and the engine components. It loads once and never reloads (ADR-0004). The source generator ships as `Fenrir.Scripting.Generators`.
- **One script assembly per project.** Its path comes from a new `.feproj` field. Scripts are built externally (`dotnet build`), and the engine watches the output.
- **Load order** is all-or-nothing:
  1. Register C# components and events. The managed side reports each type's full name, size and alignment through an internal call.
  2. Instantiate the systems in a managed runner, which native code holds as one Iris `Object`.
  3. Validate every native-backed mapping.

  On any failure, the previous generation keeps running.
- **Dispatch**: one sequential native system per phase calls the runner, which runs that phase's C# systems in `Order`. C# systems run as one block within each phase.
- **Exceptions**: the runner catches each system's exceptions, logs them with the system name through the engine logger, and disables that system until the next reload.
- **Threading**: every Iris call, every script call and every internal call happens on the main thread. Script-driving systems are always registered as sequential systems. Parallel C# systems are a separate ticket.

### Hot reload

- A reload happens only between frames, once the watcher sees a stable new DLL.
- Windows locks a loaded assembly file (review-01 D5), so the engine copies the script assembly's output to a per-generation directory and loads that copy.
- The order is:
  1. Run the old systems' `OnDestroy`.
  2. Release the old generation, then confirm it unloaded (Iris ticket 23).
  3. Load the new generation.

  If the new generation fails to load, the old one is reloaded.
- C# component pools and C# event queues are kept when their full name and layout fingerprint (size, plus each field's name, offset and kind) are unchanged. Otherwise they are dropped and a warning is logged. System instances are always recreated.

### Build

- New package `packages/FenrirScripting`, built only with `FENRIR_ENABLE_SCRIPTING`. It owns the Iris `Host`, the runtime component registry, the byte pools, the playback of deferred changes, the event bridge and the watcher. Its entry point is `Scripting::Install(App&, ScriptingConfig)`.
- Builds with scripting OFF never discover or invoke `dotnet`.
- Verification uses a headless test target with a fixture script assembly, running an `App` for N frames. DemoApp gets a sample script assembly as a smoke test only.

## Iris prerequisites

These are in `.scratch/dotnet-scripting/issues/`:

- **20**: `bool`, `char`, enums and inline arrays in structs and internal calls.
- **21**: explicit struct-mapping validation.
- **22**: member enumeration and attributes. This is for the editor and is not needed for v1.
- **23**: unload wait and collect.

## Out of scope (future work)

- A behaviour layer (per-entity script objects dispatched by one system).
- Parallel C# systems and read-only access markers, with concurrency tests (ticket 12).
- Interleaving C# and C++ systems within a phase, and `UpdateBefore`/`UpdateAfter`.
- Field-by-field migration of changed component layouts on reload.
- Editor inspector, component serialization, and ScriptableObject/Resource-style assets (these need Iris 22).
- Editor "Build scripts" button (ticket 13).
- A Roslyn analyzer for accidental copies, and C++-to-C# code generation.
- Multiple script assemblies per project.
- Input, physics and rendering APIs, once those packages exist. Each one is its own ticket using the binding pattern.
- Renaming Iris's "Component" if the two senses keep colliding.

## Tickets

See `issues/`. The frontier at the start is Iris 20 and 21 together with engine 01, then engine 02 and 03.
