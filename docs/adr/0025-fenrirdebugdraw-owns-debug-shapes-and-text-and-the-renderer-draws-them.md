---
status: accepted
---

# FenrirDebugDraw owns debug shapes and text, and the renderer draws them

Game code wants to see raycasts, aim, paths and trajectories, and FenrirPhysics wants to show colliders, contacts and joints. Both need lines, shapes and text on screen without owning GPU buffers. Today physics hands out a span of its own packed `DebugLine`s that the DemoApp copies into one `GL_LINES` pass. That code was written before a renderer existed and is rewritten to the shape below. Debug drawing gets its own package, **FenrirDebugDraw**, with no GPU code: producers (game code, C#, FenrirPhysics) draw into it, and FenrirRenderer reads it. It is the same split as ADR-0017, where FenrirInput owns input and FenrirWindow feeds it.

## The package

- **FenrirDebugDraw depends only on FenrirMath, FenrirScene and FenrirApp.** It links no library and knows nothing of the renderer, physics or assets. It is built in every configuration with no compile-out macro or enable flag. With nothing submitted, its output is empty and the renderer culls the debug and text passes.
- **A `DebugDraw` scene resource per scene.** Items are in that scene's world space, and are drawn into every view of the scene. They end with the scene.
- **It works with no renderer.** Items record, durations expire and groups toggle; nothing is drawn. That is the headless and server case, and how physics tests its debug output.
- **Parallel systems write to per-thread buffers**, merged in system registration order when the batch ends, like events' send buffers. Sequential systems write straight in. The renderer reads on the main thread.

## The calls

Immediate calls, as rich as Unreal's `DrawDebug*`:

- **Wire shapes:** line, ray, arrow, polyline (open or closed), point (a screen-sized dot), box (axis-aligned, or oriented by a transform), sphere, capsule, cylinder, cone, circle, arc, plane, grid, axes (a transform's frame) and frustum (from a view-projection). Round shapes take a segment count with a default.
- **Solid shapes:** box, sphere, capsule, and caller triangle lists, with alpha; depth tested without depth write, unsorted.
- **Text:** screen text at a pixel position; on-screen messages, stacked newest first in the top-left corner, where an optional integer key replaces the message shown under that key; and world text anchored at a world point, drawn at a constant pixel size, facing the screen, never occluded, and skipped behind the camera.
- **Every call takes:** a `Colour`, a group, an optional duration and an optional `relativeTo` entity. Shapes also take a thickness in pixels (default 1) and a depth mode: depth tested (the default) or on top.

## Lifetimes and groups

- **Duration 0 (the default) draws once:** until the next tick when submitted from `PreTick`, `Tick` or `PostTick`, otherwise until the end of this frame's `Render`. A line drawn in `Tick` then neither vanishes on a frame with no tick nor doubles on a frame with two. Edges in input follow the phase the same way (ADR-0017); Bevy's gizmos keep fixed-update drawing until the next fixed update for the same reason.
- **A positive duration** keeps the item that many seconds of frame time. The engine has no pause or time scale yet; when one comes, durations follow game time.
- **`DebugDraw::Forever`** keeps it until it is cleared. `Clear()` drops every item, and `Clear(group)` drops one group's. There are no handles to single items.
- **`relativeTo`** puts the call's positions in that entity's local space. The item follows the entity's world transform each frame while it lives, and is dropped when the entity is despawned.
- **Groups** are named, defaulting to `Default`. Each can be shown, hidden (still recorded, not drawn) or cleared. They carry no style; depth and thickness stay per call.

## How it is drawn

- **FenrirDebugDraw tessellates.** Each frame it turns every live, shown item into plain-data batches: line vertices and triangle vertices, each batch tagged with its depth mode and thickness, and a list of text runs (string, anchor, pixel size, colour, screen or world). Shapes are tested without a renderer, and any consumer gets the same data.
- **FenrirRenderer's debug render feature** uploads the batches into per-frame ring buffers (ADR-0019) and draws lines and world text in each view's `Debug` stage, against `SceneDepth`. Lines are expanded to screen-space quads in the vertex shader, since GL core has no wide lines. Screen text and messages draw in the `Overlay` stage of every window showing a view of the scene, after `Output`, so they are not tonemapped.
- **One built-in font.** The renderer embeds one OFL-licensed monospace TrueType font and rasterises it at start-up into a one-channel signed-distance-field atlas with stb_truetype, linked `PRIVATE` (ADR-0010). One atlas serves every pixel size. It covers printable ASCII and Latin-1, drawing `?` for any other codepoint. Glyph layout is the renderer's.
- **`Fenrir::Colour`** is a new FenrirMath type: four linear floats, made from sRGB with `Colour::FromSrgb(0xRRGGBBAA)`, with named constants. Debug draw takes it and the renderer packs it for vertices.

## Physics draws into it

FenrirPhysics depends on FenrirDebugDraw. Its tick systems draw each world's debug output into the world's scene's `DebugDraw`, in a `Physics` group, with duration 0, so the lines live exactly until the next step. It keeps its category flags (`Colliders`, `Contacts`…) to decide what to generate; groups only decide what is shown. `PhysicsWorld::DebugLines()`, the packed `DebugLine` and `DebugColour`'s packed colours go. That rewrite is the physics map's work.

C# gets the same calls through FenrirDebugDraw's own scripting extension (ADR-0009), built only when scripting is on.

## Considered options

- **Debug draw inside FenrirRenderer, physics producing spans, glue copying them.** No new package, but every producer either depends on the renderer or needs glue, physics debug output cannot be tested without a renderer, and the line type is defined twice.
- **Frame-only lifetimes (Unity's `duration` 0, im3d).** Simpler, but drawing from a fixed tick flickers or doubles, and shapes that should stay a few seconds must be redrawn by hand.
- **Durations and handles to single items.** Removing one item is rarely needed; groups and `Clear(group)` cover it without handle bookkeeping.
- **Compiling it out of release builds (Unreal's Shipping).** Saves a little, but debug views are wanted in release builds too, and the cost with nothing drawn is near zero.
- **A raster atlas per size, a build-time baked atlas, or a fixed bitmap font.** A raster atlas needs one atlas per size and blurs as world text; a baked atlas needs another build tool; a bitmap font scales only in whole steps. MSDF is sharpest, but needs msdfgen.
- **A packed `0xRRGGBBAA` everywhere, as physics has.** Compact, but leaves the engine without a colour type for materials and mixes sRGB and linear.

## Consequences

- The physics map gets a note: rewrite FenrirPhysics's debug output to draw into FenrirDebugDraw.
- Not yet specified: drawing a loaded mesh asset (`Mesh(AssetHandle<Mesh>)`, which would make FenrirDebugDraw depend on FenrirAssets), loaded fonts, and which windows show a scene's screen text when several do (ticket 16).
- Ticket 22 (the renderer's scripting extension) no longer covers debug lines and text.
