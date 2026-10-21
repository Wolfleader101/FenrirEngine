# How are debug lines and text drawn?

Type: grilling
Status: resolved
Blocked by: 13

## Question

How does game code and another package's plain data get debug lines and text on screen?

- Lines (and points?) per frame, over the scene or depth-tested; from a span of plain data such as FenrirPhysics's `DebugLine`s, without the caller owning GPU buffers; who glues physics's span to the renderer (app code, a demo helper).
- Text in screen space and at world positions; font, size and colour; one built-in font or loaded fonts.
- Immediate mode (submitted each frame) or retained; from C++ and C#.
- In every build or debug builds only; cost when unused.

## Answer

ADR: `docs/adr/0025-fenrirdebugdraw-owns-debug-shapes-and-text-and-the-renderer-draws-them.md`. Glossary: FenrirDebugDraw, Debug draw item, Debug group, On-screen message, Colour.

- **Home:** a new FenrirDebugDraw package (FenrirMath, FenrirScene, FenrirApp; no GPU code, no library) owns a `DebugDraw` scene resource. Game code, C# and FenrirPhysics draw into it; FenrirRenderer reads it in `Render`. Built in every configuration, no flag; with no renderer, items still record and expire.
- **Calls, as rich as Unreal's:** wire line, ray, arrow, polyline, point, box (AABB or oriented), sphere, capsule, cylinder, cone, circle, arc, plane, grid, axes, frustum; solid box, sphere, capsule and caller triangle lists with alpha. Each takes a `Colour`, a group, an optional duration and an optional `relativeTo` entity; shapes also a pixel thickness (default 1) and depth tested (default) or on top.
- **Lifetimes:** duration 0 draws once, until the next tick when submitted in a tick phase, else until the end of `Render`; seconds of frame time; or `Forever` until `Clear()` / `Clear(group)`. No handles to single items. `relativeTo` items follow their entity and die with it.
- **Groups:** named (default `Default`), shown, hidden or cleared; no style.
- **Text:** screen text at pixel positions and stacked on-screen messages (a key replaces the message under it) in the `Overlay` stage of each window showing the scene; world text at a constant pixel size, facing the screen, never occluded, in the view's `Debug` stage. Durations, groups and `relativeTo` as shapes.
- **Drawing:** FenrirDebugDraw tessellates live items into plain line and triangle batches (tagged with depth mode and thickness) and text runs; the renderer uploads them into per-frame ring buffers, expands lines to screen-space quads, and lays out glyphs.
- **Font:** one embedded OFL monospace font, rasterised at start-up into one SDF atlas with stb_truetype (`PRIVATE`), any pixel size, printable ASCII and Latin-1 with `?` otherwise. Loaded fonts are later.
- **Colour:** `Fenrir::Colour` in FenrirMath, linear float RGBA, `FromSrgb(0xRRGGBBAA)` and named constants.
- **Threads:** parallel systems write per-thread buffers merged in registration order, like event send buffers.
- **Physics:** draws from its tick systems into its scene's `DebugDraw` in a `Physics` group with duration 0, keeping its category flags for what to generate; `PhysicsWorld::DebugLines()`, the packed `DebugLine` and packed `DebugColour` go. A note on the physics map, not a ticket here.
- **C#:** FenrirDebugDraw's own scripting extension (ADR-0009), specified in the foundation spec; ticket 22 drops debug lines and text.
- **Later** (*Not yet specified*): `Mesh(AssetHandle<Mesh>)`, loaded fonts, a pause or time scale for durations. Multi-window screen text goes to ticket 16. No new tickets; stb_truetype joins the build as ticket 21 decides.
