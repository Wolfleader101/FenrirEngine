# FenrirDebugDraw

Debug shapes and text, kept per scene as plain data (ADR-0025). Game code, C# and other packages (FenrirPhysics) draw
into a scene's `DebugDraw`; FenrirRenderer draws what it holds. It links no library, has no GPU code, and is built in
every configuration, with no flag to turn it off. Without a renderer, items still record, expire and toggle, which is
how tests check them.

## Turning it on

```cpp
Fenrir::DebugDrawing debugDrawing = Fenrir::DebugDrawing::Install(app); // keep it alive while the app runs
```

`Install` gives every scene a `DebugDraw` scene resource: the scenes there are now at once, and a scene made later at
the start of the next frame. It adds one sequential `PreUpdate` system, the expiry system, which drops expired items.
It cannot fail. Destroying the owner removes every scene's `DebugDraw`.

## Drawing

```cpp
Fenrir::DebugDraw& draw = *scene.GetResource<Fenrir::DebugDraw>();
draw.Line(from, to, Fenrir::Colour::Red);
draw.Sphere(centre, 0.5f, Fenrir::Colour::Green, {.duration = 2.0f, .segments = 32});
draw.Arrow(origin, target, 0.25f, Fenrir::Colour::Yellow, {.thickness = 3.0f, .depth = Fenrir::DepthMode::OnTop});
draw.Message(1, "speed: 12 m/s", Fenrir::Colour::White);
```

- **Wire shapes:** `Line`, `Ray`, `Arrow`, `Polyline` (open or closed), `Point` (a dot of a pixel size), `Box` (an
  `AABB`, or centre, half extents and rotation), `Sphere`, `Capsule`, `Cylinder`, `Cone`, `Circle`, `Arc`, `Plane`,
  `Grid`, `Axes` (red, green and blue) and `Frustum` (from a view-projection, in ADR-0026's reversed-Z depth).
- **Solid shapes,** with the colour's alpha, depth tested without writing depth and not sorted: `SolidBox`,
  `SolidSphere`, `SolidCapsule` and `Triangles` (the caller's, three vertices each).
- **Text:** `ScreenText` at a pixel position, `Message` (on-screen messages, stacked newest first from the window's
  top-left; a message with a key replaces the message under the same key, in its place), and `WorldText` at a world
  point.

Every call takes a `Colour` and options. Shapes take `ShapeOptions`: group, duration, `relativeTo`, thickness in pixels
(1), depth mode (`Tested`, or `OnTop`) and the segment count of round shapes (24). Text takes `TextOptions`: group,
duration, `relativeTo` and size in pixels (16).

## Lifetimes

- **Duration 0** (the default) draws once. Drawn from `PreTick`, `Tick` or `PostTick`, the item lasts until the next
  tick begins, so it neither vanishes in a frame with no tick nor doubles in a frame with two. Drawn from any other
  phase, it lasts until the end of this frame's `Render`.
- **A positive duration** keeps the item that many seconds of frame time.
- **`DebugDraw::Forever`** keeps it until `Clear()` or `Clear(group)`. There are no handles to single items.
- **`relativeTo`** puts the positions in that entity's space: the item follows its world transform and is dropped
  when the entity is destroyed. Screen text and messages stay where they are, but are dropped with it too.

## Groups

Items belong to a named group, `Default` unless the options name another. `HideGroup` hides a group's items without
stopping them recording or expiring; `ShowGroup` shows them again; `Clear(group)` drops them. Groups carry no style.

## Threads

From a system the scheduler runs in parallel, an item is held in the system's send buffer until its batch ends, then
recorded in the order the systems were registered, as events are. Sequential systems record directly. Read
`Tessellate` on the main thread, from a sequential system, as the renderer does in `Render`.

## From C#

With `FENRIR_ENABLE_SCRIPTING` on, `FenrirDebugDrawScripting` (`Scripting/`) gives C# the `Fenrir.DebugDraw` contract
(`Managed/Fenrir.DebugDraw/`, ADR-0029): add `DebugDrawing::ScriptingExtension()`
(`FenrirDebugDrawScripting/DebugDrawScripting.hpp`) to `ScriptingConfig::extensions`, and call
`fenrir_deploy_debugdraw_scripting(<target>)` beside `fenrir_deploy_scripting(<target>)`. It needs no owner, so scripts
always load.

```csharp
using Fenrir.DebugDraw;

DebugDraw.Line(from, to, Colour.Red);
DebugDraw.Sphere(centre, 0.5f, Colour.Green, new ShapeOptions { Duration = 2.0f, Segments = 32 });
DebugDraw.Message(1, $"speed: {speed:0} m/s", Colour.White);
```

- **`DebugDraw`** (write `Fenrir.DebugDraw.DebugDraw` inside a `Fenrir.*` namespace) is a static facade of native
  functions, not a mirror (ADR-0029): every C++ call above, by the same name, into the active scene's `DebugDraw`, so
  lifetimes, groups and tessellation are C++'s own. It is the only way C# draws debug shapes; physics's contract keeps
  only `PhysicsWorld.SetDebugDraw(PhysicsDebugCategories)`, which picks what physics draws into the `Physics` group.
- **Options:** `ShapeOptions` and `TextOptions` are structs whose `new` is C++'s `{}` (group `Default`, duration 0,
  `relativeTo` `Entity.None`, thickness 1, `DepthMode.Tested`, 24 segments, size 16); leaving them out is the same.
  `DebugDraw.Forever` is the forever duration. Points and triangles are `ReadOnlySpan<Vector3>`, text is any
  `ReadOnlySpan<char>`, and math is `Fenrir.Math`'s.
- **Lifetimes from C#:** duration 0 drawn from a `Phase.Tick` system lasts until the next tick, and from any other
  phase until the end of the frame's `Render`, as in C++.
- **No-op rules:** with no `DebugDraw` in the active scene (debug drawing not installed) or a `relativeTo` entity that is
  not alive, a call draws nothing and `IsGroupShown` is false; nothing throws, and a debug build warns once per native
  call site and reason.

## What a renderer reads

`DebugDraw::Tessellate()` returns `DebugBatches`: the live items of shown groups in world space, with `relativeTo`
items placed by their entity now.

- **Line batches,** two vertices per line, each batch with one depth mode and thickness. A point is a line whose two
  vertices are the same, `thickness` pixels across.
- **Triangle batches,** three vertices per triangle, each with one depth mode.
- **Text runs:** screen text, then on-screen messages newest first, then world text.

Layout, the font and how each is drawn are the renderer's.
