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

## What a renderer reads

`DebugDraw::Tessellate()` returns `DebugBatches`: the live items of shown groups in world space, with `relativeTo`
items placed by their entity now.

- **Line batches,** two vertices per line, each batch with one depth mode and thickness. A point is a line whose two
  vertices are the same, `thickness` pixels across.
- **Triangle batches,** three vertices per triangle, each with one depth mode.
- **Text runs:** screen text, then on-screen messages newest first, then world text.

Layout, the font and how each is drawn are the renderer's.
