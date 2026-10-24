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

- **Where .NET comes from.** The Iris bridge (`Iris.Bridge.dll`, its `.runtimeconfig.json`), `nethost`, the
  scripting contract `Fenrir.Scripting.dll` and the math contract `Fenrir.Math.dll` must sit next to the executable, or
  in `ScriptingConfig::bridgeDirectory`. In CMake, call `fenrir_deploy_scripting(<target>)` on the executable.
- **The contract.** `Fenrir.Scripting` (`Managed/Fenrir.Scripting`) is the C# API scripts compile against: `Entity`,
  `Log` and `Time` so far. `Scripting::Install` always shares it with Iris, as one copy in the default load context
  that is never reloaded, so its types have one identity across every load and reload of the script assembly. A
  script project references it with `<Private>false</Private>` (compile against it, do not copy it). The version is
  part of the contract: a script built against a newer minor than the host's fails to load.
- **The math contract.** `Fenrir.Math` (`packages/FenrirMath/Managed/Fenrir.Math`, namespace `Fenrir.Math`, ADR-0029)
  holds `Vector2`, `Vector3`, `Vector4`, `Quaternion`, `Matrix4x4`, `Colour`, `Ray` and `Bounds`, each laid out as its
  FenrirMath counterpart (`Math::Vec2` to `Math::Mat4`, `Fenrir::Colour`, `Math::Ray`, `Math::AABB`). It is pure managed
  code with no native functions, so any package's contract can use it. The scripting contract is built on it
  (`Transform` holds its `Vector3` and `Quaternion`), and `Scripting::Install` shares it first, before
  `Fenrir.Scripting`. A script names it with `using Fenrir.Math;`. Inside a `Fenrir.*` namespace, `Math` then means the
  namespace, so write `System.Math` (or `MathF`) for the BCL's.
- **One active Scripting.** The engine's native functions act on the App of the `Scripting` that is active, so a
  second `Install` while one is alive logs an error and runs without scripts.
- **The script assembly path** comes from the app, `ScriptingConfig::scriptAssembly`. Empty means no scripts, and .NET
  is not started at all. `examples/ScriptingDemo` is a working example, with its own README on building and running it;
  the demo scaffold passes each demo app's built scripts project (`examples/DemoScaffold/README.md`, "C# scenes").
- **Shutdown.** Everything Iris owns is released when the app finishes `Run` (a sequential system in the `Exit`
  phase, on the main thread), and again, harmlessly, when the `Scripting` object is destroyed. Destroy it before
  `main` returns.

## Writing a script project

A script project needs only the scripting SDK: one folder holding `Fenrir.Scripting.dll` (the contract),
`Fenrir.Math.dll` (the math contract it is built on), `analyzers/Fenrir.Scripting.Generators.dll` (the source generator) and `Fenrir.Scripting.props`. CMake builds it into
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

The props file references the two contracts for compilation only (the host shares its own copies at run time), turns on
runs the generator (the field accessors are C# 14 extension properties, the default for net10.0). A script's output must not carry its own copy of `Fenrir.Scripting.dll` or `Fenrir.Math.dll`.

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
- **What gets a property.** Public and internal instance fields. A `readonly` field gets a `ref readonly` property, so
  a system holding `Ref<T>` to write the rest reads it in place, and assigning to it is a compile error (`CS8331`,
  checked by `RefCompile-ReadOnlyFieldWrite`): that is how a component keeps a part only the engine writes. Not
  exposed: static and `const` fields, and fixed buffers. A field named `Value` or `IsNull` would be shadowed by
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

They are the built-in extension's, and generated like any package's (ADR-0042): each is written once, in FenrirECS's
headers, and its C# mirror, mapping and registration come from FenrirECS's API description (see "Adding to the engine's
own API").

```csharp
var t = new Entity(id).Get<Transform>();        // a Ref<Transform> into the engine's storage
t.Position.X += 1;                              // visible to C++ through GetComponent<Transform>() at once
new Entity(id).Get<Name>().Text = "Player";     // a string in, UTF-8 in the shared buffer
var parent = new Entity(id).GetReadOnly<Relationship>().Parent;   // an EntityLink; check IsNull, then use Target
bool has = new Entity(id).Has<Transform>();
```

### Entity links

An `Entity` inside a component or event is an `EntityLink` in C#: read `IsNull`, then `Target`. In a component C# may
write, a link is made by assigning an `Entity`, so every native component with an `Entity` field is writable from C#:

```csharp
var tether = holder.Get<Tether>();
tether.Target = door;                 // a link to door in the active scene; C++ reads it as door's Entity
tether.Target = EntityLink.None;      // the empty link; C++ reads a default Entity
var created = Entity.Create();
tether.Target = created;              // fine: a created entity's value is reserved at once
```

- **A link names an entity of the active scene.** The conversion stores the active scene's entity list beside the value,
  where a native `Entity` keeps it (the `EntityLinkOwner` native function gives it). Scripts never see that part: an
  `EntityLink` shows only `IsNull`, `Target` and `None`.
- **`default(EntityLink)` is not `None`.** `new EntityLink()` is `None`, as C++'s default `Entity` is, but a zeroed link
  names entity 0 with no scene, so a component made with `default` names entity 0 in each `Entity` field. C++'s `IsValid` is false for it; it does not crash, but write `EntityLink.None`
  where you mean "no entity".
- The `ScriptingTests` entity-link tests are the proof: a link C# writes reads in C++ as the same entity of the active
  scene, `None` reads as a default `Entity`, a link to an entity created earlier in the same system survives playback, a
  zeroed link reads safely, and `EntityLink` exposes nothing more.

`Get` and `GetReadOnly` throw `InvalidOperationException` with a message that says why: the entity is not alive, it has
no such component, the engine has nothing registered under the type's name, or the type has no `[NativeComponent]`
attribute. (Creating, destroying, adding and removing from C# arrives with deferred changes, ticket 06.)

`Transform` is relative to the parent. The world transform (FenrirECS `GetWorldTransform`, composed through the parents
on each call) is read and set through the entity, and both take effect at once, so a system reads its own edits:

```csharp
var e = new Entity(id);
Transform world = e.WorldTransform;              // position, rotation, component-wise scale in the scene
world.Position = new Vector3(0, 5, 0);
e.SetWorldTransform(world);                      // sets the local Transform so the world one is this
```

Both throw `InvalidOperationException` if the entity is not alive or has no `Transform`.

### Adding your own

Give scripting a C++ component through a scripting extension (see "Scripting extensions": the app's own, or its package's),
with an `Iris::StructMapping` and a C# struct of exactly the same layout carrying `[NativeComponent("Name")]`:

```cpp
struct Health { std::int32_t Current, Max; };
template <> struct Iris::StructMapping<Health> {
    static constexpr std::string_view typeName = "MyGame.Health";    // the C# struct's full name
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(Health, Current), IRIS_FIELD(Health, Max));
};

game.components.push_back(Fenrir::MakeComponent<Health>("MyGame.Health"));   // `game`: the app's ScriptingExtension
```

```csharp
[NativeComponent("MyGame.Health")]
public struct Health : IComponent { public int Current; public int Max; }
```

- **The layout is checked, and a mismatch fails.** When a script assembly loads, every registered component is validated
  against its C# struct (name, size, each field's name, offset and kind). The first mismatch fails the whole load: the
  assembly is released, the app keeps running, and the error names the component and the field.
- **Nothing registers after Install.** What scripting knows is fixed by the extensions given to `Install`; afterwards the
  registry changes only when a reload removes and adds the components and events the script assembly defines.
- **Engine types as fields.** A component, event, shared struct or resource holding a `Math::Vec3`, `Math::Quat`,
  `Transform` or `Entity` includes `FenrirScripting/Generated/FenrirScriptingMappings.hpp`, the built-in binding's
  mappings, which name them as `Fenrir.Math`'s `Vector3` and `Quaternion` and the contract's `Transform` and
  `EntityLink`. An `Entity` member then maps as itself, and C# sees it as an `EntityLink`, as in `Relationship`. The same
  header brings the hand-written mappings of FenrirMath's other types (`include/FenrirScripting/BoundMappings.hpp`):
  `Math::Vec2`, `Vec4`, `Fenrir::Colour` and `Math::Ray`, as `Fenrir.Math`'s `Vector2`, `Vector4`, `Colour` and `Ray`.
  Two take a stand-in, put where the real type is in a stand-in of the struct holding it: glm keeps a `Math::Mat4`'s
  columns private, so `Detail::Mat4View` (four `Math::Vec4` columns) is a `Matrix4x4` of `Column0` to `Column3`; and
  `Math::AABB` maps as FenrirPhysics's `Aabb` (its generated binding), so `Detail::BoundsView` is `Fenrir.Math`'s `Bounds`.
  In a writable component the link is writable too: assigning an `Entity` makes a link to it in the active scene, and
  `EntityLink.None` is the empty link (C++'s default `Entity`). See "Entity links" below.
- **Field names and order matter.** Fields are listed in the C# declaration order; `IRIS_FIELD_AS(Type, member, "Name")`
  gives a field its C# name when it differs from the C++ member (that is how `pos` is `Position`).
  This is not pedantry: it caught a real bug. `glm::quat` keeps `w` first in memory, so the C# `Quaternion` declares
  `W, X, Y, Z` in that order. Declaring `X, Y, Z, W` would have swapped every rotation silently.
- **Members a mapping cannot name** (a private buffer) are declared by offset (`Iris::Field<T>("Name", offset)`), or the
  component is described by a plain struct with the same size and layout: `MakeComponent<T, Mapping>(name)`. Size and
  alignment are checked at compile time. A generated binding needs neither: the engine's `Name` keeps its buffer private
  and `Entity` its value and entity list, and FenrirECS's API description records their layouts all the same.
- **A native component is reached only through the C# type that was validated.** Declaring your own struct with
  `[NativeComponent("Fenrir.Relationship")]` does not work: the engine hands the component out only to the type whose
  layout it checked, and `Get<T>` throws for any other.
- **Read-only components.** `MakeReadOnlyComponent` registers data scripts must not change. In C# such a struct
  implements `IReadOnlyComponent` (not `IComponent`), so there is no `Ref<T>` for it, and `ReadOnlyRef<T>` hands out
  `ref readonly` accessors: assigning through it is a compile error (`CS8331`, `CS8332`), checked by the
  `RefCompile-Relationship*` tests.
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

## Tagged unions

A component can hold a tagged union: a tag saying which kind it holds and overlapping storage for one of several kinds
(FenrirPhysics's `CollisionShape` and `JointKind` are two). Iris's struct mapping accepts C#'s explicit layout and checks
every field by name, offset and kind, and nothing stops two fields sharing an offset. On the C# side, the `[TaggedUnion]`
source generator writes the members; the author writes the tag enum and the fields:

```csharp
public enum ShapeType : byte { Box, Sphere }      // mirrors the C++ enum's values

[TaggedUnion(Noun = "shape")]
[StructLayout(LayoutKind.Explicit)]
public partial struct Shape
{
    [FieldOffset(0)] private ShapeType _type;     // the tag: the one enum field at offset 0
    [FieldOffset(4)] private BoxShape _box;       // one private field per kind, at the union's offset (the C++ offsetof)
    [FieldOffset(4)] private SphereShape _sphere;
}
```

- **The tag** is the one enum-typed instance field at offset 0, of any underlying type (the engine's unions use `byte`,
  as their C++ enums are `std::uint8_t`; the scripting tests' `Shape` uses `int`). The enum is written by hand because it
  must mirror C++'s values.
- **Every other instance field is a kind**, named `_` plus its tag member with the first letter lowered: `ShapeType.Box`
  is `_box`, `ConvexHull` is `_convexHull`. There is no other way to pair them. The native mapping names these fields.
- **The offset is written out**, in `[FieldOffset]`: C# cannot see the C++ union's alignment (the union starts at its own
  alignment after the tag). A wrong one fails the load, naming the field.
- **`Noun`** is what the wrong-view message calls the union. Left out, the message uses the struct's name.

The generator adds, to the author's partial, exactly what a hand-written union has. This is what `Shape` above expands to:

```csharp
public Shape() { _type = ShapeType.Box; _box = new(); }   // the kind whose tag value is 0, with its defaults

public readonly ShapeType Type => _type;

// A checked view per kind, named after its tag member: a reference into the shape, so writes land in the component's storage.
[UnscopedRef] public ref BoxShape Box { get { Expect(ShapeType.Box); return ref _box; } }
[UnscopedRef] public ref SphereShape Sphere { get { Expect(ShapeType.Sphere); return ref _sphere; } }

// Assigning a kind by value sets the tag with it.
public static implicit operator Shape(BoxShape value) => new() { _type = ShapeType.Box, _box = value };
public static implicit operator Shape(SphereShape value) => new() { _type = ShapeType.Sphere, _sphere = value };

private readonly void Expect(ShapeType wanted)
{
    if (_type != wanted) throw new InvalidOperationException($"The shape is a {_type}, not a {wanted}.");
}
```

So `collider.Shape = new SphereShape { Radius = 1 }` changes the kind, `collider.Shape.Box.HalfExtents.Y = 2` edits a box
in place, and reading the wrong kind's view throws `InvalidOperationException`, as C++'s asserting `As<T>()` does. It
generates nothing else (no equality, `ToString` or `Match`), and it works the same in a contract assembly and a script
assembly.

A union the generator cannot write for is a build error of its own, and gets no members, so that error is the one shown:

| Error | Refused |
| --- | --- |
| FENRIR0030 | Not a `partial struct`, or inside a type that is not partial. |
| FENRIR0031 | A `readonly` struct (it hands out writable views), a generic one, or a `ref struct`. |
| FENRIR0032 | No single enum-typed field at offset 0 to be the tag. |
| FENRIR0033 | A kind field that is not private, is `readonly`, or is not named after a tag member. |
| FENRIR0034 | A tag member with no kind field. |
| FENRIR0035 | Kinds at different offsets, or at an offset inside the tag. |
| FENRIR0036 | A kind that is not an unmanaged struct, is the union itself, or has the same type as another kind. |
| FENRIR0037 | A member the generator writes, written by hand: the parameterless constructor, `Type`, a view, a conversion from a kind, or `Expect`. |
| FENRIR0038 | A tag with no member of value 0 (the kind `new()` makes), or two members of one value. |

- **`default(Shape)` is not `new Shape()`.** It is the tag-0 kind with every setting zero, not that kind's defaults.
- **An empty kind** (a C++ `struct FixedJoint {};`, which takes one byte) maps as an empty C# struct, which is one byte
  too, through a mapping with no fields (`Iris::StructFields()`), as FenrirPhysics's `FixedJoint` and `BallJoint` do.
- **Components defined in C# may hold a union too**: their struct is their definition, so there is no native side.
- **A bool inside a kind** is fine in a component, which C# reads in place. But when Iris copies such a union from C++ by
  value (a struct argument of a C# method called from C++, or an array copy), it makes every byte where any kind has a
  bool 0 or 1, which can change another kind's bytes there. Keep bools out of the kinds of a union passed that way.

**The native side is written by hand.** The generator emits no C++. The mapping lists the tag and each kind in
declaration order, with `IRIS_FIELD_AS` for the C# names; it can reach private members by being a friend, or declare
them by offset with `Iris::Field`, which is how FenrirPhysics's generated binding maps its unions without the package
naming Iris. The load checks what couples the two sides: the field names, their order, the offset and the tag's size. Adding a kind means the C++ union, the C++ enum, the mapping, the C# enum member and one C# field.

```cpp
class Shape {
  public:
    Shape(BoxShape box);       // implicit, one per kind
    Shape(SphereShape sphere);
    ShapeType Type() const;
    template <typename T> T& As();   // asserts the kind
  private:
    friend struct Iris::StructMapping<Shape>;
    ShapeType m_type;
    union { BoxShape m_box; SphereShape m_sphere; };
};
template <> struct Iris::StructMapping<Shape> {
    static constexpr std::string_view typeName = "MyGame.Shape";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Shape, m_type, "_type"),
        IRIS_FIELD_AS(Shape, m_box, "_box"), IRIS_FIELD_AS(Shape, m_sphere, "_sphere"));
};
```

The `ScriptingTests` tagged-union tests are the proof: a native component holding a generated union validates,
round-trips through a C# system both ways, throws for the wrong kind with its noun or its type's name, and a hand-written
mirror with a wrong offset or name is refused. The `TaggedUnion*` compile cases cover a byte and an `int` tag, an empty
kind and a nested union, and one case per refusal.

## Creating, destroying, adding and removing

```csharp
var bullet = Entity.Create();                       // usable at once
bullet.Add(new Velocity { Linear = new Vector3(0, 0, 30) });
bullet.Add<Tagged>();                               // a default (all-zero) component
other.Remove<Velocity>();
victim.Destroy();
```

These work for native and C# components alike, on the active scene. They are **recorded, not applied**: the ECS storage does
not change until the system that made them returns: the runner plays them back after each system (and `OnCreate`), and drops
them if a script throws. That is what keeps every `Ref<T>` a script took valid for the whole call, even when
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
- **From C++.** Playback and discarding belong to the runner, and the tests reach them through `ScriptingProbe`. A native
  function C# calls records its own structural changes in the same buffer, through its call context (see "The change
  buffer from a native function"). Native code that changes the ECS directly is not buffered.
- **Resources too.** `Resources.Insert` and `Resources.Remove` (see "Native resources") are recorded and played back in
  the same order as everything else.

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
- `SystemContext` has `Time`, `Log`, `CreateEntity()`, `Query<...>()` and `Events` (sending; see Events).

## Events

```csharp
public struct Damage : IEvent { public Entity Target; public float Amount; }   // C#-only: nothing else needed

[System(Phase.Update, Order = 10)]
public sealed class Reporter : ISystem
{
    private readonly EventReader<Damage> damage = new();   // one reader per event type the system reads

    public void OnUpdate(in SystemContext ctx)
    {
        foreach (var hit in damage.Read())
            ctx.Log.Info($"{hit.Target} took {hit.Amount}");
        ctx.Events.Send(new Damage { Target = default, Amount = 1 });
    }
}
```

An event is an unmanaged struct implementing `IEvent`. The engine keeps one queue per event, and a system reads it through an
`EventReader<T>` it keeps in a field, which yields each event once: the same reader, and the same guarantee, as a C++ system's
`EventReader<T>` (ADR-0012). A reader remembers how far it has read, so a read yields only the events sent since its previous
read, including ones sent after it last ran; readers of one queue, C# or C++, are independent of each other.

- **C#-only events need no C++.** The SDK generator lists them in an `EventTable`; when the script assembly loads, the engine
  makes a queue for each (the App's `CreateEventQueue`), and removes it when the assembly is released.
- **Sharing a C++ event.** List it in an extension's `events` with `Fenrir::MakeEvent<T>("Ns.Name")` and mirror it in
  C# with `[NativeEvent("Ns.Name")]`. The layout is checked like a component's, and a mismatch fails the load
  naming the field. `SendEvent<T>`/`EventReader<T>` in C++ and `Events.Send<T>()`/`EventReader<T>` in C# are then one queue.
  The C++ type must be trivially copyable and default constructible: events cross the boundary as bytes.
- **Sends wait for the system to return.** `Events.Send` is recorded like a structural change, so what a read yielded stays
  valid for the whole system. The systems after it, and C++, see the send; the sender's own readers see it on their next read.
- **Reading.** `reader.Read()` gives an `EventRange<T>` (a `ref struct`, so it cannot outlive the system): `Count`, an
  indexer, `foreach`, and `Missed`, how many events the reader lost by going two of the queue's swaps without reading (the
  queue swaps at the end of each frame that ran a tick, so zero for a reader that reads every frame). Elements come out
  oldest first; `var` copies one, `ref readonly var` does not. A reader in a `Tick` system sees an
  event on its first run after the send, even across frames that run no tick.
- **A new reader**, such as one made by a reload, starts at the oldest event the queue still holds and reports nothing
  missed. After a reload one may see an event the previous generation's reader already handled just before it.
- **By name from C++.** `App::NameEvent<T>("name")` gives a typed queue a name, `App::FindEventQueue("name")` finds any named
  queue, and `IEventQueue::SendRaw` and `ReadRawAfter` send it and read it as bytes, with a cursor the caller keeps (one
  per reader, starting at `NewReaderCursor`). A raw send from a system running in parallel is held until its batch has
  run, as `SendEvent` is.

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

An engine package, or the app, makes itself scriptable with a `ScriptingExtension` (ADR-0009, amended by ADR-0029),
which the app lists in `ScriptingConfig::extensions`. FenrirScripting never names the package. An extension carries:

- **native functions**: C++ functions C# calls, made with `NativeFunction::Make<&Function>("Namespace.Type.Method")`, named
  by the full name of the C# method that calls them. They may record structural changes through the call context's change
  buffer;
- **components and events**: native components (`MakeComponent`, `MakeReadOnlyComponent`) and C++ events (`MakeEvent`),
  registered before the script assembly loads and checked against it as it does;
- **shared structs**: the plain data its native functions take or return by pointer that is neither a component nor an
  event (a query filter, a hit), made with `MakeSharedStruct<T>("Namespace.Type")`. Each is checked against its C#
  mirror when a script assembly loads, as a native component is: a mismatch fails the load naming the struct and field,
  and the app runs without scripts. The names of every extension's components, events, shared structs and resources
  together are unique;
- **extension state**: one object its native functions work on, given with `SetState(std::shared_ptr<State>)` (setting
  it again replaces it). Scripting keeps it alive until it shuts down. It is the only way a native function reaches its
  package's state: no process globals;
- **native resources**: plain-data scene and app resources (`MakeSceneResource`, `MakeAppResource` and their read-only
  variants), registered and checked the same way, which C# reads and writes in place through `Resources`;
- **shared assemblies**: its C# contract, shared with every script load. A relative path is in the bridge directory,
  beside `Fenrir.Scripting.dll`, where the build deploys a package's contract (FenrirPhysics's
  `fenrir_deploy_physics_scripting`, say).

Extensions are the only way to give scripting anything, so the app's own components, resources, events and contract go in
an extension of its own, just as a package's do:

```cpp
Fenrir::ScriptingExtension game;
game.name = "MyGame";
game.components = {Fenrir::MakeComponent<Health>("MyGame.Health")};
game.events = {Fenrir::MakeEvent<Explosion>("MyGame.Explosion")};
game.resources = {Fenrir::MakeSceneResource<Weather>("MyGame.Weather")};
config.extensions.push_back(std::move(game));
```

A native function's first parameter is the `CallContext`, which gives the App, the active scene's entities and the change
buffer, and nothing of any extension. A native function that needs its extension state takes it second, as a reference:

- `R F(const CallContext&, Args...)`: a stateless native function;
- `R F(const CallContext&, State&, Args...)`, where `State` is a class type: it receives the object its extension's
  `SetState` was given.

Its other parameters and its result are what C# passes and gets: sized numbers, `float`, `double`, `bool`, `char16_t`,
pointers, and `std::string_view` parameters. It is called only while a Scripting is running. Otherwise C# gets a zero result, or
the value given as `Make`'s second argument. It may read and write component and resource data at once, but makes
structural changes only by recording them (see "The change buffer from a native function"): the calling system holds
`Ref<T>`s into the storage.

```cpp
float HeightOf(const Fenrir::CallContext& context, std::uint32_t entity) { /* context.Entities() ... */ }
bool Raycast(const Fenrir::CallContext& context, PhysicsScriptState& physics, const Ray* ray, RaycastHit* hit);

Fenrir::ScriptingExtension physics;
physics.name = "physics";
physics.sharedAssemblies = {"Fenrir.Physics.dll"};    // beside Fenrir.Scripting.dll
physics.SetState(std::make_shared<PhysicsScriptState>());
physics.functions = {Fenrir::NativeFunction::Make<&HeightOf>("Fenrir.Physics.Queries.HeightOf"),
                     Fenrir::NativeFunction::Make<&Raycast>("Fenrir.Physics.Queries.Raycast")};
physics.sharedStructs = {Fenrir::MakeSharedStruct<Ray>("Fenrir.Physics.Ray"),
                         Fenrir::MakeSharedStruct<RaycastHit>("Fenrir.Physics.RaycastHit")};
config.extensions.push_back(std::move(physics));
```

`Install` registers every extension's native functions before it shares the contracts. It fails, logged and running
without scripts, if a C# field has no native function, if their signatures differ, if two extensions declare one name
(a native function, a component, an event, a shared struct or a resource), or if a native function takes an extension
state its extension does not carry (none, or one of another type: the diagnostic names the function and both types).
The tests' test-only extension (`Tests/Extension`, used by `Tests/ExtensionScript`) is a complete example, and
FenrirPhysics's is a real one: `PhysicsScriptingExtension()` (in its `FenrirPhysicsScripting` library, so FenrirPhysics
never links FenrirScripting), whose natives take its extension state, its contract
`packages/FenrirPhysics/Managed/Fenrir.Physics`, and its tests through this package's harness in
`packages/FenrirPhysics/Scripting/Tests`. FenrirInput's and FenrirWindow's are static (`Input::ScriptingExtension()`,
`Window::ScriptingExtension()`): they need no installed package, and the window's contract builds on input's. Their
extensions, and those of FenrirAssets, FenrirRenderer and FenrirDebugDraw, are still written by hand, without a generated
binding.

### Native resources

A plain-data resource crosses as a mirror, as a component does (ADR-0029). The extension registers the C++ type with a
name; the contract declares a blittable C# struct of exactly the same layout with `[NativeResource("Name")]`:

```cpp
struct Weather { float Wind = 0.5F; std::int32_t Gusts = 0; };          // a scene resource: Scene::AddResource<Weather>
template <> struct Iris::StructMapping<Weather> {
    static constexpr std::string_view typeName = "MyGame.Weather";       // the C# struct's full name
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(Weather, Wind), IRIS_FIELD(Weather, Gusts));
};

game.resources = {Fenrir::MakeSceneResource<Weather>("MyGame.Weather"),
                  Fenrir::MakeReadOnlyAppResource<Clock>("MyGame.Clock")};
```

```csharp
[NativeResource("MyGame.Weather")]
public struct Weather : IResource { public float Wind; public int Gusts; }

[NativeResource("MyGame.Clock")]
public struct Clock : IReadOnlyResource { public double Seconds; }

var weather = Resources.Get<Weather>();                    // a Ref<Weather> into the active scene's resource
weather.Wind += 1;                                         // C++ sees it at once
double now = Resources.GetReadOnly<Clock>().Seconds;       // a ReadOnlyRef: assigning through it does not compile
if (Resources.TryGet<Weather>(out var maybe)) { /* ... */ }
Resources.Insert(new Weather { Wind = 2 });                // recorded: replaces the resource when the system returns
Resources.Remove<Weather>();                               // recorded too
```

- **Scope.** `MakeSceneResource` mirrors `Scene::GetResource<T>()`: C# always reaches the *active* scene's, so changing
  the active scene changes what `Get` returns. `MakeAppResource` mirrors `App::TryGetResource<T>()`. A writable resource
  must be trivially copyable (C# writes it as bytes); every resource must be default constructible, since `Insert<T>()`
  makes one as C++ constructs it.
- **The layout is checked, and a mismatch fails the load**, naming the resource and the field, exactly as for a component.
  `Make...Resource<T, Mapping>` takes a stand-in mapping for members Iris cannot map, as `MakeComponent` does.
- **Absent is normal.** `TryGet` and `TryGetReadOnly` return false when the resource is not there now. `Get` and
  `GetReadOnly` on an absent resource log a warning naming it and return an empty view (`IsNull`; `Value` on it throws).
  A type no extension registered, or one claiming a registered name with a struct of its own, throws
  `InvalidOperationException` saying which.
- **Inserting and removing are structural changes.** They are recorded and played back in order with the system's other
  changes, so a `Ref<T>` taken before stays valid, and a read sees the state as it is now. A read-only resource has no
  `Get`, `Insert` or `Remove` in C# at all (`CS0315`), and writing through its `ReadOnlyRef` is `CS8331`; the
  `RefCompile-*Resource*` cases check both.
- **Access works from any C# code**, a system or a helper it calls. There are no resource system parameters.
- **A resource is not a component.** `IResource` and `IReadOnlyResource` extend the component interfaces only so that
  `Ref<T>`, `ReadOnlyRef<T>` and the generated field accessors serve both; `Entity.Add<Weather>()` and the like throw, and
  the generator never reports a resource as a script component.

### Facades: resources with behaviour

A resource whose answers depend on the calling phase or on calls (input devices, whose just-pressed edges follow the
phase; debug drawing, which tessellates) is **not** mirrored. Its package gives C# a static class of native functions with
the C++ class's name, each asking C++:

```csharp
public static partial class Keyboard
{
    [NativeFunction]
    public static partial bool JustPressed(Key key);      // C++'s Keyboard answers, with its phase rule
}
```

The rule: **a resource whose data is its API is mirrored; a resource with methods is a facade.** So C# never
re-implements a rule C++ owns, and the two can never drift. A facade function follows the no-op rules of its package when
the resource is absent (nothing happens, and C# gets `false`, zero or an invalid value).

### The change buffer from a native function

A native function may create and destroy entities, parent one under another, add and remove components, and insert and
remove resources, but only by recording them in the change buffer its call context gives (`context.Changes()`). It is
the buffer `Entity.Create`, `Add`, `Remove`, `Destroy` and `Resources.Insert` record into, so everything plays back
together, in the order it was recorded, when the calling system returns:

```cpp
std::uint32_t SpawnCharged(const Fenrir::CallContext& context, std::int32_t level)
{
    const auto changes = context.Changes();
    const std::uint32_t entity = changes.CreateEntity();          // alive at once: usable in this call
    changes.AddComponent(entity, Charge{level});                  // played back after the system
    changes.InsertSceneResource(Weather{2.0F, 1});
    return entity;                                                // C# gets a usable Entity
}
```

- **A created entity is usable at once**: alive, so its id can be returned to C# and given components in the same call. It
  gets its Transform, Name and Relationship at playback, before the changes recorded after it.
- **Everything else waits**: `DestroyEntity`, `AddChild` (the child becomes the parent's last child, as
  `Entity::AddChild` makes it; use it on an entity with no parent, such as one just created), `AddComponent` (which
  replaces a component the entity has), `RemoveComponent`, `InsertSceneResource`, `RemoveSceneResource`,
  `InsertAppResource` and `RemoveAppResource`. Reads through `context.Entities()` see the state as it is now.
- **Any component type**, registered with scripting or not, since native code names its C++ type. A change on an entity
  that is not alive is refused (false); one naming an entity an earlier change destroyed is skipped at playback with a
  warning.
- **Direct structural changes stay forbidden**: never call `CreateEntity`, `AddComponent`, `AddResource` and the like on
  the live storage from a native function, because the calling system's `Ref<T>`s point into it.

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

An extension with a generated binding (ADR-0042) writes only step 1: `tools/describe-api --natives` reads its C++
natives into a natives description, and `Iris.Bindings` generates steps 2 and 3 from it, in one order (its README,
"Natives"). The C++ function then needs external linkage, outside an anonymous namespace, since the generated
registration names it from another file. In its doc, `@param[out]` makes a pointer an `out` parameter, `@param[in,out]` a
pointer C# passes as it is, and `@notrunning <expression>` the value C# gets when no Scripting is running (`Make`'s
second argument). The engine's own natives (`src/Bindings.cpp`) and FenrirPhysics's
(`packages/FenrirPhysics/Scripting/src/PhysicsScripting.cpp`) are built that way.

## Adding to the engine's own API

The engine's own API (Log, Time, Entity, components, events, queries, changes, resources) is the built-in scripting
extension: its native functions are the methods of the contract's `Native` class, declared like any package's. It also
carries the engine's components (Transform, Name and Relationship, all required) and, as its shared assemblies,
`Fenrir.Math` and then the `Fenrir.Scripting` contract built on it. It is always the first extension, so the engine's
components take their names before any other extension's, and the two contracts are shared before any other.

It is a generated binding (ADR-0042), as FenrirPhysics's is: `api/FenrirScripting.binding.toml` binds FenrirECS's and
FenrirMath's API descriptions (`packages/FenrirECS/api`, `packages/FenrirMath/api`) and the natives description
`api/FenrirScripting.natives.json`, which describe-api reads from `src/Bindings.cpp`. From them `Iris.Bindings` writes the
contract's mirrors and `Native` class (`Managed/Fenrir.Scripting/Generated`), the mappings
(`include/FenrirScripting/Generated`), the extension's lists (`src/Generated`) and two tests (`Tests/Generated`). Only the
vector and quaternion, bound types whose C# types are `Fenrir.Math`'s, are mapped by hand
(`include/FenrirScripting/BoundMappings.hpp`, beside FenrirMath's other types). To add a native
function, in the order a script sees it:

1. **The C++ function**, in `src/Bindings.cpp`'s `Fenrir::ScriptingNatives`, over the `CallContext` like any native
   function, with a doc comment: it becomes the C# declaration's. The engine's own state (the component registry, the
   change buffer) is the built-in extension's extension state, the `EngineContext`, which every one of its native
   functions takes second. A C++ exception that escapes it is stopped by Iris and logged; scripts get a zero result.
   `@notrunning` gives C# something other than zero when no Scripting is running.

   ```cpp
   /**
    * @brief Writes `text` to the engine's logger at `level`: 0 Info, 1 Warn, 2 Error.
    */
   void Log(const CallContext& context, Detail::EngineContext&, std::int32_t level, const char* text,
            std::int32_t length);
   ```

2. **Regenerate** (`fenrir-describe-api`, then `fenrir-generate-bindings`): its `[NativeFunction]` declaration in `Native`
   (`void Log(int level, string text)`) and its registration, `Fenrir.Scripting.Native.<Method>`, are generated.

3. **A friendly wrapper** beside it that scripts use (`Native` is internal, and passes pointers through):

   ```csharp
   public static class Log { public static void Info(string? message) => Native.Log(0, message ?? ""); }
   ```

Iris binds every generated field of the shared contract when it is shared, and **sharing fails** (so no scripts load, and
the engine logs why) if a method has no C++ function or a different signature. A mismatch can therefore never become a
null call. Rules to remember:

- The contract has `[assembly: DisableRuntimeMarshalling]`, which is what makes a `bool` one byte and a `char` two.
- A native function must not assume a thread: every script call happens on the main thread today, but the function
  should only touch what the App owns, and (see "The change buffer from a native function") must not make structural
  changes except through the change buffer, as the engine's own entity functions do.
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
