# FenrirScripting

C# scripting for FenrirEngine, built on [Iris](../Iris/README.md). The design is in
[ADR-0007](../../docs/adr/0007-ecs-first-csharp-scripting.md) and `.scratch/engine-scripting/spec.md`.

The package exists only when the root build is configured with `-DFENRIR_ENABLE_SCRIPTING=ON`. With it OFF,
CMake never looks for .NET, never adds Iris or this package, and never runs `dotnet`.

## Using it

```cpp
Fenrir::App app(std::make_unique<Fenrir::ConsoleLogger>());
// Declared after the app: .NET is released before the app is destroyed.
auto scripting = Fenrir::Scripting::Install(app, Fenrir::ScriptingConfig{.scriptAssembly = "scripts/MyGame.dll"});
app.Run();
```

`Scripting::Install` starts the Iris `Host`, forwards Iris's log messages to `app.Logger()` (tagged `[Iris]`), and
loads the one script assembly. It never throws, and a missing or invalid assembly, or a missing runtime, is
logged as an error and the app runs without scripts (`HasScripts()` is false).

- **Where .NET comes from.** The Iris bridge (`Iris.Bridge.dll`, its `.runtimeconfig.json`), `nethost` and the
  scripting contract `Fenrir.Scripting.dll` must sit next to the executable, or in
  `ScriptingConfig::bridgeDirectory`. In CMake, call `fenrir_deploy_scripting(<target>)` on the executable.
- **The contract.** `Fenrir.Scripting` (`Managed/Fenrir.Scripting`) is the C# API scripts compile against: `Entity`,
  `Log` and `Time` so far. `Scripting::Install` always shares it with Iris, as one copy in the default load context
  that is never reloaded, so its types have one identity across every load and reload of the script assembly. A
  script project references it with `<Private>false</Private>` (compile against it, do not copy it). The version is
  part of the contract: a script built against a newer minor than the host's fails to load.
- **One active Scripting.** The engine's native functions act on the App of the `Scripting` that is active, so a
  second `Install` while one is alive logs an error and runs without scripts.
- **The script assembly path** comes from the project: the `scriptAssembly` field of the `.feproj` (DemoApp reads it
  into `ProjectSettings`). Empty means no scripts, and .NET is not started at all. `examples/DemoApp` is a working example, with
  its own README on building and running it.
- **Shutdown.** Everything Iris owns is released when the app finishes `Run` (a sequential system in the `Exit`
  phase, on the main thread), and again, harmlessly, when the `Scripting` object is destroyed. Destroy it before
  `main` returns.

## Writing a script project

A script project needs only the scripting SDK: one folder holding `Fenrir.Scripting.dll` (the contract),
`analyzers/Fenrir.Scripting.Generators.dll` (the source generator) and `Fenrir.Scripting.props`. CMake builds it into
`<build>/fenrir-scripting/contract/`. Import the props file and nothing else is needed:

```xml
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework>
    <ImplicitUsings>enable</ImplicitUsings>
  </PropertyGroup>
  <Import Project="$(FenrirScriptingSdk)Fenrir.Scripting.props" />   <!-- dotnet build -p:FenrirScriptingSdk=<folder>/ -->
</Project>
```

The props file references the contract for compilation only (the host shares its own copy at run time), turns on
runs the generator (the field accessors are C# 14 extension properties, the default for net10.0). A script's output must not carry its own copy of `Fenrir.Scripting.dll`.

### Components and `Ref<T>`

A component is an `unmanaged` struct implementing `IComponent`. `Ref<T>` is a `ref struct` over a pointer into the
engine's storage, and for every component the generator adds one extension property per public (or internal)
instance field, returning `ref` to that field:

```csharp
public struct Vec3 { public float X, Y, Z; }
public struct Transform : IComponent { public Vec3 Position; public float Scale; }

void Nudge(Ref<Transform> t)
{
    t.Position.X += 1;     // writes the engine's memory: no `ref` at the call site
    t.Scale *= 2;
    Transform copy = t.Value;   // the whole component (a copy when assigned to a variable)
}
```

- **How it works.** The generated code is a C# 14 `extension(Ref<Transform> self)` block with
  `public ref Vec3 Position => ref self.Value.Position;`. A property that returns `ref` is a variable, so
  `t.Position.X += 1` assigns through it. The `t.Value.Position.X` spelling the spec kept as a fallback also works,
  but is not needed.
- **What gets a property.** Public and internal instance fields. Not exposed: static and `const` fields, `readonly`
  fields (there is no `ref` to give), and fixed buffers. A field named `Value` or `IsNull` would be shadowed by
  `Ref<T>`'s own members, so the generator warns (`FENRIR0001`) and it stays reachable as `t.Value.Value`. A generic
  component warns (`FENRIR0002`). A keyword-named field (`@class`) works. A nested or internal component gets an
  accessors class of the matching accessibility. A `partial` component works, but keep all of its fields in one
  declaration: the compiler warns (`CS0282`) that field order across declarations is undefined, and a component's
  layout is what native code relies on.
- **A `Ref<T>` is only good for the call that was handed it.** It points into storage the engine may move, so the
  compiler refuses every way of keeping one. Each is a compile-fail test: a field of a class (`CS8345`), a lambda
  capturing it (`CS9108`), `List<Ref<T>>` (`CS9244`) and an `async` method taking it (`CS4012`), against a control case
  that compiles.
- **A `Ref<T>` can be null** (`IsNull`); using `Value` on one throws `InvalidOperationException` naming the type.

## Native components

A component in the ECS is exposed to C# by name. The ECS has no registry of types by name, so FenrirScripting keeps the
table: each entry pairs a name and a C# struct with the component's `ComponentType` (which `EntityList` uses to get, add,
remove, count and query it) and says how to check its layout against the C# struct.

| Name | C# type | Native type | Access |
| --- | --- | --- | --- |
| `Fenrir.Transform` | `Transform` (`Position`, `Rotation`, `Scale`) | `Fenrir::Transform` | read and write |
| `Fenrir.Name` | `Name` (`Text`, a fixed UTF-8 buffer of 256 bytes) | `Fenrir::Name` | read and write |
| `Fenrir.Relationship` | `Relationship` (`Parent`, `FirstChild`, `NextSibling`, `PrevSibling`) | `Fenrir::Relationship` | **read only** |

```csharp
var t = new Entity(id).Get<Transform>();        // a Ref<Transform> into the engine's storage
t.Position.X += 1;                              // visible to C++ through GetComponent<Transform>() at once
new Entity(id).Get<Name>().Text = "Player";     // a string in, UTF-8 in the shared buffer
var parent = new Entity(id).GetReadOnly<Relationship>().Parent;   // an EntityLink; check IsNull, then use Target
bool has = new Entity(id).Has<Transform>();
```

`Get` and `GetReadOnly` throw `InvalidOperationException` with a message that says why: the entity is not alive, it has
no such component, the engine has nothing registered under the type's name, or the type has no `[NativeComponent]`
attribute. (Creating, destroying, adding and removing from C# arrives with deferred changes, ticket 06.)

### Adding your own

Register a C++ component from anywhere, with an `Iris::StructMapping` and a C# struct of exactly the same layout carrying
`[NativeComponent("Name")]`:

```cpp
struct Health { std::int32_t Current, Max; };
template <> struct Iris::StructMapping<Health> {
    static constexpr std::string_view typeName = "MyGame.Health";    // the C# struct's full name
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(Health, Current), IRIS_FIELD(Health, Max));
};

scripting.RegisterComponent<Health>("MyGame.Health");   // or ScriptingConfig::components, before the assembly loads
```

```csharp
[NativeComponent("MyGame.Health")]
public struct Health : IComponent { public int Current; public int Max; }
```

- **The layout is checked, and a mismatch fails.** When a script assembly loads, every registered component is validated
  against its C# struct (name, size, each field's name, offset and kind). The first mismatch fails the whole load: the
  assembly is released, the app keeps running, and the error names the component and the field. Registering after the
  assembly loaded is checked at once, with the same all-or-nothing result.
- **Field names and order matter.** Fields are listed in the C# declaration order; `IRIS_FIELD_AS(Type, member, "Name")`
  gives a field its C# name when it differs from the C++ member (that is how `pos` is `Position`).
  This is not pedantry: it caught a real bug. `glm::quat` keeps `w` first in memory, so the C# `Quaternion` declares
  `W, X, Y, Z` in that order. Declaring `X, Y, Z, W` would have swapped every rotation silently.
- **Members Iris cannot map** (a private buffer, a type holding a pointer) can be described by a plain struct with the same
  size and layout: `MakeComponent<T, Mapping>(name)`. Size and alignment are checked at compile time. `Relationship` does
  this, because an `Entity` holds a pointer to its entity list (C# sees the 32-bit entity value and keeps the rest opaque),
  and so does `Name`, whose buffer is private.
- **A native component is reached only through the C# type that was validated.** Declaring your own struct with
  `[NativeComponent("Fenrir.Relationship")]` does not work: the engine hands the component out only to the type whose
  layout it checked, and `Get<T>` throws for any other.
- **Read-only components.** `MakeReadOnlyComponent` (or `RegisterReadOnlyComponent`) registers data scripts must not
  change. In C# such a struct implements `IReadOnlyComponent` (not `IComponent`), so there is no `Ref<T>` for it, and
  `ReadOnlyRef<T>` hands out `ref readonly` accessors: assigning through it is a compile error (`CS8331`, `CS8332`),
  checked by the `RefCompile-Relationship*` tests.
- **A caution for native code.** `Fenrir::Entity(id, list)` is a *creating* constructor: it adds a Transform, a Name and a
  Relationship, replacing existing ones. To look an entity up without touching it, use `EntityList::GetEntity(id)`, which is
  what the registry does.

## Components defined in C#

A script can define components with no C++ at all:

```csharp
public struct Velocity : IComponent { public Vector3 Linear; }

var v = new Entity(id).Get<Velocity>();
v.Linear += new Vector3(1, 2, 3);          // writes the engine's storage, like any component
```

The SDK's generator writes a table into the script assembly listing every `IComponent` struct it defines, and the engine
runs that table as the assembly loads. Each component is reported with its full name, its size and its alignment as C#
lays it out, and the engine registers it under that name.

- **Storage belongs to the ECS.** FenrirScripting does not store anything itself and never touches the ECS library
  ([ADR-0008](../../docs/adr/0008-fenrirecs-owns-the-ecs-library.md)). It describes the component to `FenrirECS` as a
  dynamic `ComponentType` (a `DynamicComponentType`: name, size, alignment) and uses `EntityList`'s `Get`, `Has`, `Add`,
  `Remove`, `Count` and `Clear`, as it does for a native component. The ECS stores such components like any other: removed
  with the entity, found by a query beside native ones. A new component is all zero bytes (a C# `default`). A test fails the build if scripting code names the ECS library.
- **Components do not move when the storage grows.** The ECS keeps them in pages of up to 256 (and at most 64 KiB a page), each
  page allocated once and aligned to the component's alignment (a `Vector128` field makes it 16). Removing one moves the last into its place, so a
  pointer is good until a component of that type is next removed.
- **Storage is created lazily, per scene.** Registering a component creates nothing; each scene makes the storage the first
  time the component is added in it, and asking about a component nobody added creates nothing. So a load that is thrown
  away leaves nothing behind.
- **A storage id is a hash of the name, and the ECS defends it.** If another name hashes to the same id, or the id belongs to
  a native storage, the later user is refused and the first is untouched (tested with two real colliding names).
- **From native code**, `scripting.FindComponent("Ns.Velocity")` gives the registration. Its `type` goes to `EntityList`'s
  `Get`, `Has`, `Add`, `Remove` and `Count`, and to `EntityList::ForEachWith(types, func)` (every entity with all of them,
  native and script components mixed). `type.Size()`, `type.Alignment()` and `scriptDefined` say what it is. The same
  works for a native component.
- **Rules.** A component is an `unmanaged` struct implementing `IComponent`; one holding a reference is refused when the
  script compiles (`FENRIR0004`). It needs no `[NativeComponent]` attribute (a struct that has one mirrors a native
  component and is not reported). Its name is its full name, so it must not equal a native component's name.

### Loading is all or nothing

A script assembly's load is a transaction. In order: the components it defines are discovered and checked; every native
component's mapping is checked against its C# struct; and only then are the script's components registered. If any step
fails, the assembly is released and **nothing** from that load is registered: no registry entries and no storage. (The checks
today are a malformed report, a name that clashes with a native component or another script component, two names that would
share a storage, and a native mismatch. Systems will join the order when they exist.) The error says which.

## Creating, destroying, adding and removing

```csharp
var bullet = Entity.Create();                       // usable at once
bullet.Add(new Velocity { Linear = new Vector3(0, 0, 30) });
bullet.Add<Tagged>();                               // a default (all-zero) component
other.Remove<Velocity>();
victim.Destroy();
```

These work for native and C# components alike, on the active scene. They are **recorded, not applied**: the ECS storage does
not change until the native code that called into the script gets control back and calls `Scripting::ApplyChanges()` (the
runner will do it after each system). That is what keeps every `Ref<T>` a script took valid for the whole call, even when
other entities gain or lose the same component. Reads (`Get`, `Has`, `IsValid`) are never recorded and see the state as it
is *now*, so a component added a line ago is not there yet, and an entity just destroyed is still alive.

- **Order.** Changes play back in the order they were made. `Add(value)` sets the value (replacing one that is there);
  `Add<T>()` keeps an existing component and otherwise adds a default one.
- **A created entity** gets its Transform, Name and Relationship at playback, like any entity the engine makes. Until then it
  is alive with no components.
- **Destroy then add** (or any change after a destroy) on one entity: the entity stays destroyed, the later change is skipped
  with a warning, and it never reaches whatever reuses the slot. Destroying twice is harmless.
- **Failing clearly.** `Get<T>` on a missing component or a dead entity, and any change on a dead entity or an unregistered
  component, throw `InvalidOperationException` saying which. Transform and Name cannot be removed (the engine assumes every
  entity has them), and read-only components such as Relationship have no `Add` or `Remove` at all.
- **From C++.** `ApplyChanges()` (returns how many were applied and skipped) and
  `DiscardChanges()` (drops them, and destroys the entities they created). Native code that changes the ECS directly is not
  buffered.

## Queries

```csharp
foreach (var (entity, transform, velocity) in World.Query<Transform, Velocity>())
    transform.Position += velocity.Linear;           // writes the engine's own memory

World.Query<Counter>().ForEach((Entity entity, Ref<Counter> counter) => counter.Hits += 1);
```

A query visits every entity that has all the listed components (1 to 8 of them, native and C# mixed), in no particular order.
Each row deconstructs into the `Entity` and a `Ref<T>` per component. `ctx.Query<...>()` will forward to `World.Query` once
systems have a context (ticket 08).

- **Cost.** A query makes two native calls (an upper bound on the rows, then the rows) however many entities match: native code
  runs the ECS query and fills a scratch buffer of the entity and one component address per type. Measured in a Release
  build: 10,000 entities with Transform and Velocity took about 0.5 ms for the whole `Integrate` loop.
  The tests count the calls through `ScriptingProbe`, so a test can check it.
- **Changes during iteration** are deferred (see above), so a loop that removes, adds, creates or destroys never disturbs the
  rows it is walking. Queries nest; each has its own rows. Leaving a loop early is fine.
- **Empty and invalid.** A query with no matches is empty and creates no storage. A component the engine has not registered,
  or the same component listed twice, throws `InvalidOperationException` saying which. Read-only components (Relationship)
  cannot be queried yet: queries are read-write.
- **The generated part** is written by the SDK's source generator into the contract itself (`Query<...>`, `QueryRow<...>`,
  `QueryAction<...>`, arities 1 to 8), because each arity has its own shape.

## Systems

```csharp
[System(Phase.Update, Order = 10)]
public sealed class Mover : ISystem
{
    public void OnUpdate(in SystemContext ctx)
    {
        foreach (var (entity, transform, velocity) in ctx.Query<Transform, Velocity>())
            transform.Position += velocity.Linear * ctx.Time.DeltaTime;
    }
}
```

A system is a class with a public or internal parameterless constructor that implements `ISystem` and carries `[System(Phase, Order)]`.
The phases are `PreUpdate`, `Tick`, `Update` and `PostUpdate`, in the order a frame runs them; `Tick` runs once per fixed step.
Within a phase, the lowest `Order` runs first (ties by full name). `OnCreate` and `OnDestroy` are optional.

- **Found at build time.** The SDK's generator lists the systems in a generated `SystemHost`, with no reflection. `[System]` on a
  class that is not an `ISystem` (FENRIR0010), that the engine cannot construct (abstract, generic, no accessible
  parameterless constructor: FENRIR0011) or that names a phase that does not exist (FENRIR0013) is a build error. An `ISystem` without `[System]` is a warning (FENRIR0012): it would
  never run.
- **What the engine does.** `Scripting::Install` makes the `SystemHost` (one managed object), runs every system's `OnCreate`,
  and registers one *sequential* native system for each phase that has systems. Each frame the phase system calls into the
  script **once**, and the runner runs that phase's systems in order. The tests count those calls through `ScriptingProbe`.
  `OnDestroy` runs, last system first, when the app finishes (the Exit phase) or on `Scripting::Shutdown()`, before the
  script assembly is released.
- **Changes land between systems.** After every system (and every `OnCreate`/`OnDestroy`) the runner plays back what that system
  recorded, so the next system sees a created entity or a new component.
- **A throwing system.** The exception is logged once with the system's name, the changes that call recorded are dropped (and
  the entities it created are destroyed), and that system does not run again until the next reload. Other systems carry on.
- **Main thread only.** Scripts run on the thread that installed scripting (the one running the App's sequential systems).
  A phase asked to run elsewhere asserts in a debug build and is skipped, with an error, in a release build. This is not
  covered by a test, because the debug assert aborts the process.
- `SystemContext` has `Time`, `Log`, `CreateEntity()` and `Query<...>()`. Events join it in ticket 09.

## Events

```csharp
public struct Damage : IEvent { public Entity Target; public float Amount; }   // C#-only: nothing else needed

[System(Phase.Update, Order = 10)]
public sealed class Reporter : ISystem
{
    public void OnUpdate(in SystemContext ctx)
    {
        foreach (var hit in ctx.Events.Read<Damage>())
            ctx.Log.Info($"{hit.Target} took {hit.Amount}");
        ctx.Events.Send(new Damage { Target = default, Amount = 1 });
    }
}
```

An event is an unmanaged struct implementing `IEvent`. The engine keeps one queue per event with the same double buffering as
C++ `SendEvent`/`ReadEvents`: a read sees this frame's events so far and the last frame's, and the queues rotate once a frame.

- **C#-only events need no C++.** The SDK generator lists them in an `EventTable`; when the script assembly loads, the engine
  makes a queue for each (the App's `CreateEventQueue`), and removes it when the assembly is released.
- **Sharing a C++ event.** Register it with `Scripting::RegisterEvent<T>("Ns.Name")` (or `ScriptingConfig::events`) and
  mirror it in C# with `[NativeEvent("Ns.Name")]`. The layout is checked like a component's, and a mismatch fails the load
  naming the field. `SendEvent<T>`/`ReadEvents<T>` in C++ and `Events.Send/Read<T>()` in C# are then one queue. The C++ type
  must be trivially copyable and default constructible: events cross the boundary as bytes.
- **Sends wait for the system to return.** `Events.Send` is recorded like a structural change, so a system never reads its own
  sends and a view from `Events.Read<T>()` stays valid for the whole system. The systems after it, and C++, see the send.
- **Reading.** `Read<T>()` gives an `EventReader<T>` (a `ref struct`, so it cannot outlive the system): `Count`, an indexer
  and `foreach`. Elements come out oldest first; `var` copies one, `ref readonly var` does not.
- **By name from C++.** `App::NameEvent<T>("name")` gives a typed queue a name, `App::FindEventQueue("name")` finds any named
  queue and reads or sends it as bytes (`IEventQueue::ReadRaw/SendRaw`). Existing `SendEvent`/`ReadEvents` code is unchanged.

## Hot reload

Build the script project while the game runs and the new code takes over between frames, with the game's data intact.

- **The assembly is never loaded in place.** `Scripting::Install` copies the script assembly (and its `.pdb`, `.deps.json` and the
  dlls the deps file lists) into a private per-generation folder and loads the copy, so the build is free to overwrite the original
  even on Windows, which locks a loaded dll. The folder is deleted once that generation has been collected.
  (`ScriptingConfig::reloadDirectory` chooses where; the default is under the system temp folder.)
- **The watcher** (`ScriptingConfig::hotReload`, on by default) looks at the file at the start of each frame, at most every
  `reloadInterval`. A new build is loaded once it has stayed unchanged and openable for `reloadDebounce`, so a half-written dll is
  never loaded. `Scripting::Reload()` does the same on demand (call it between frames, not from a script).
- **The order** is: play back anything a script left recorded; the old systems' `OnDestroy`; release the old generation and wait for
  the runtime to confirm it unloaded (`unloadTimeout`; if a root still holds it the reload goes on, warns that the generation has not
  unloaded, and `PendingUnloadCount()` stays above zero); then load the new generation. System instances are always recreated, so
  `OnCreate` runs again.
- **A broken build keeps the game running.** If the new generation does not load (not a valid assembly, a native component or event
  that no longer matches its C# struct, ...) the error is logged and the previous copy is loaded again. Nothing was touched before the
  checks passed, so its data is as it was. The same broken file is not retried until it changes.
- **What survives.** A script component's data stays in the ECS, and a C#-only event's queue (with the events in it) stays in the App,
  when its *fingerprint* is unchanged: size, alignment, and each field's name, type, kind and offset. Anything whose fingerprint
  changed, or that the new build no longer defines, is removed with a warning that says how many components went (a component) or that
  the queue was dropped (an event), before any system runs, so nothing ever reads old bytes as a new layout. A struct that is not
  public cannot be inspected and is compared by size and alignment alone.
- **Limits.** The fingerprint does not look inside a nested struct, so changing the fields of a nested type without changing the outer
  struct's size or offsets is not noticed. Native components and events (the ones C++ registers) are not reloaded: they belong to the engine.

## Scripting extensions

An engine package, or the app, makes itself scriptable with a `ScriptingExtension` (ADR-0009), which the app lists in
`ScriptingConfig::extensions`. FenrirScripting never names the package. An extension carries:

- **native functions**: C++ functions C# calls, made with `NativeFunction::Make<&Function>("Namespace.Type.Method")`, named
  by the full name of the C# method that calls them;
- **components and events**, exactly like `ScriptingConfig::components` and `events`;
- **shared assemblies**: its C# contract, shared with every script load.

A native function's first parameter is the `CallContext`, which gives the App and the active scene's entities. Its other
parameters and its result are what C# passes and gets: sized numbers, `float`, `double`, `bool`, `char16_t`, pointers,
and `std::string_view` parameters. It is called only while a Scripting is running. Otherwise C# gets a zero result, or
the value given as `Make`'s second argument. It may read and write component data, but must not create or destroy
entities or add or remove components: the calling system holds `Ref<T>`s into the storage.

```cpp
float HeightOf(const Fenrir::CallContext& context, std::uint32_t entity) { /* context.Entities() ... */ }

Fenrir::ScriptingExtension physics;
physics.name = "physics";
physics.sharedAssemblies = {"Fenrir.Physics.Scripting.dll"};
physics.functions = {Fenrir::NativeFunction::Make<&HeightOf>("Fenrir.Physics.Queries.HeightOf")};
config.extensions.push_back(std::move(physics));
```

`Install` registers every extension's native functions before it shares the contracts. It fails, logged and running
without scripts, if a C# field has no native function, if their signatures differ, or if two extensions declare one name.
The tests' test-only extension (`Tests/Extension`, used by `Tests/ExtensionScript`) is a complete example.

## Exposing a native function

A native function is two declarations, one per side, tied together by the C# method's full name, plus a line in an
extension.

1. **The C++ function**, taking the `CallContext` first:

   ```cpp
   float HeightOf(const Fenrir::CallContext& context, std::uint32_t entity) { /* context.Entities() ... */ }
   ```

2. **The C# declaration**, in the extension's contract: a `static partial` method marked `[NativeFunction]`, in a partial
   type. The source generator writes its body and the function pointer field the host binds (`__native_HeightOf`, by the
   binding-name rule `NativeFunction::BindingNameFor`). The contract project needs `AllowUnsafeBlocks`.

   ```csharp
   public static partial class Queries
   {
       [NativeFunction]
       public static partial float HeightOf(Entity entity);
   }
   ```

3. **The line in the extension**: `NativeFunction::Make<&HeightOf>("Fenrir.Physics.Queries.HeightOf")`.

What crosses, and how:

| C# | C++ |
| --- | --- |
| sized integers, `float`, `double`, `bool`, `char`, pointers | the same (`char16_t` for `char`); `bool` and `char` need `[assembly: DisableRuntimeMarshalling]` |
| `Entity` | `std::uint32_t`, its value |
| `string` or `ReadOnlySpan<char>` parameter | `std::string_view`, UTF-8 (a lone surrogate becomes U+FFFD), valid for the call |
| `in`, `ref` or `out` unmanaged struct | a pointer to it, valid for the call |

A string is never a result, and a struct does not cross by value. The generator refuses at build time a native function that
is not `static partial` (FENRIR0020, FENRIR0021), is in a type that is not partial (FENRIR0022), is generic (FENRIR0023),
shares its name with another in its type (FENRIR0024), returns a string (FENRIR0025) or uses a type that cannot cross
(FENRIR0026). Your friendly API calls the declared method like any other, and adds behaviour (such as turning error codes
into exceptions) only where it wants.

## Adding to the engine's own API

The engine's own API (Log, Time, Entity, components, events, queries, changes) is the built-in scripting extension: its
native functions are the methods of the contract's `Native` class, declared like any package's. To add one, in the order
a script sees it:

1. **The C++ function**, in `src/Bindings.cpp`, over the `CallContext` like any native function. The engine's own state
   (the component registry, the change buffer) comes from the context's internal part. A C++ exception that escapes it is
   stopped by Iris and logged; scripts get a zero result.

   ```cpp
   void Log(const CallContext& context, std::int32_t level, const char* text, std::int32_t length) { /* ... */ }
   ```

2. **Its entry in `BuiltInExtension`**, named `Fenrir.Scripting.Native.<Method>`. Give a second argument if C# should get
   something other than zero when no Scripting is running:

   ```cpp
   NativeFunction::Make<&Log>(prefix + "Log"),
   ```

3. **The declaration in `Managed/Fenrir.Scripting/Native.cs`**: a `[NativeFunction]` method with the same parameters,
   plus a **friendly wrapper** beside it that scripts use (UTF-8, lengths and `fixed` stay in the wrapper, because
   `Native` is internal and passes pointers through):

   ```csharp
   [NativeFunction]
   public static partial void Log(int level, byte* text, int length);     // in Native (internal)

   public static class Log { public static void Info(string? message) => Write(0, message); /* UTF-8, length, fixed */ }
   ```

Iris binds every generated field of the shared contract when it is shared, and **sharing fails** (so no scripts load, and
the engine logs why) if a method has no C++ function or a different signature. A mismatch can therefore never become a
null call. Rules to remember:

- The contract has `[assembly: DisableRuntimeMarshalling]`, which is what makes a `bool` one byte and a `char` two.
- A native function must not assume a thread: every script call happens on the main thread today, but the function
  should only touch what the App owns, and (see "Scripting extensions") must not make structural changes except through
  the change buffer, as the engine's own entity functions do.
- Test it with a method on the fixture script assembly (`Tests/Fixture/Fixture.cs`) that calls the new wrapper, and a
  `TEST_CASE` in `Tests/ScriptingTests.cpp` that calls that method with `Call<...>` and checks what the engine saw.
- A package outside FenrirScripting does not edit any of this: it uses its own extension ("Scripting extensions").

## Tests

`FenrirScriptingTests` runs an `App` with no window for a few frames around a small fixture script assembly
(`Tests/Fixture`), built with the same .NET SDK Iris uses against the scripting SDK folder. The `RefCompile-*` tests
build the small projects in `Tests/CompileFail` and check the compiler's verdict. The tests are Catch2 test cases, so they
build only with `FENRIR_BUILD_TESTS=ON` (and need the `libs/catch2` submodule: `git submodule update --init libs/catch2`).
Run everything with CTest:

```sh
cmake -S . -B out/build/engine-scripting -DFENRIR_ENABLE_SCRIPTING=ON -DFENRIR_BUILD_TESTS=ON
cmake --build out/build/engine-scripting --config Debug --target FenrirScriptingTests
ctest --test-dir out/build/engine-scripting -C Debug --output-on-failure
```

The tests find the assemblies CMake built in the environment (`FENRIR_TEST_FIXTURE`, `FENRIR_TEST_FIXTURE_V2`,
`FENRIR_TEST_BAD_REPORTS`, `FENRIR_TEST_EXECUTABLE_DIR`), which CTest sets. To run one case by hand, set those (`ctest -V -R
FenrirScripting` shows them) and give `FenrirScriptingTests` Catch2's options: a test name, the `"[scripting]"` tag or
`--list-tests`, and `--order decl`, because some cases rely on the order they are declared in. Checks are plain `REQUIRE`s
with an `INFO` message.

The counters and the loaded Iris component the tests need are not on `Scripting`; the tests read them through
`ScriptingProbe` (`Tests/ScriptingProbe.hpp`), which `Scripting` names as a friend.

`Tests/HeadlessApp.hpp` is the harness later tickets build on: `HeadlessApp` owns an `App` and a recording
logger, and `Run(n)` runs exactly `n` frames.

The first configure of a fresh build directory can fail in the third-party assimp project; running the same
configure command again succeeds. That happens with scripting OFF too.
