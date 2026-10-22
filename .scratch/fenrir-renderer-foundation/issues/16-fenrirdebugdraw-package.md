# 16: FenrirDebugDraw: debug shapes, text, durations and groups, headless

**What to build:** FenrirDebugDraw exists as an always-built package with no library and no GPU code: a `DebugDraw` scene resource records wire and solid shapes, screen text, keyed on-screen messages and world text, with durations, groups and `relativeTo` entities, and tessellates them into plain batches (spec: "FenrirDebugDraw", "Testing Decisions" seam 6; ADR-0025, ADR-0026). The renderer draws the batches in ticket 17; physics moves onto it in ticket 18.

- **Package** `packages/FenrirDebugDraw`, depending on FenrirMath, FenrirScene and FenrirApp only; built in every configuration with no compile-out or enable flag. `Install(App&)` adds the expiry and merge systems and returns its owner; the ticket names the owner type (`DebugDraw` is the resource's name).
- **A `DebugDraw` scene resource** per scene, in that scene's world space. With no renderer, items still record, expire and toggle.
- **Calls:** wire line, ray, arrow, polyline (open or closed), point (screen-sized), box (AABB or oriented), sphere, capsule, cylinder, cone, circle, arc, plane, grid, axes and frustum (round shapes take a segment count with a default); solid box, sphere, capsule and caller triangle lists, with alpha. Every call takes a `Colour`, a group, an optional duration and an optional `relativeTo` entity; shapes take a pixel thickness (default 1) and a depth mode (depth tested by default, or on top).
- **Lifetimes:** duration 0 lasts until the next tick when submitted from `PreTick`, `Tick` or `PostTick`, else until the end of this frame's `Render`; a positive duration lasts that many seconds of frame time; `DebugDraw::Forever` lasts until `Clear()` or `Clear(group)`. No item handles. `relativeTo` items are in the entity's local space, follow its world transform and die with it.
- **Groups:** named, defaulting to `Default`; shown, hidden (still recorded) or cleared; no style.
- **Text:** screen text at a pixel position; on-screen messages stacked newest first from the window's top-left, a key replacing the message under it; world text at a world point. (Drawing rules are the renderer's, ticket 17.)
- **Threads:** parallel systems write per-thread buffers merged in registration order when the batch ends; sequential systems write directly.
- **Tessellation:** live, shown items into plain batches: line and triangle vertices tagged with depth mode and thickness, plus text runs (screen, message and world).
- README for the package.

**Blocked by:** 01 (Engine prerequisites); 31 (The current phase and the frame and tick counts).

**Touches:** `packages/FenrirDebugDraw/`, `CMakeLists.txt`

**Status:** ready-for-agent

- [ ] `FenrirDebugDrawTests` (headless, through `App::RunFrame`): tessellated batches for a line, a box, a sphere with a given segment count, an arrow and a solid triangle list match expected vertices, tagged with depth mode and thickness.
- [ ] Durations per phase: a `Tick` item survives a frame with no tick and is gone after the next tick; with two ticks in a frame it does not double; an `Update` item lasts to the end of that frame's `Render`; seconds expire on frame time; `Forever` lasts until `Clear` or its group's `Clear`.
- [ ] A hidden group still records and reappears when shown; `Clear(group)` clears only it.
- [ ] `relativeTo` items follow the entity's world transform and are gone when it is destroyed.
- [ ] Parallel systems' items merge in registration order; messages replace by key and stack newest first.
- [ ] Builds in every configuration, including `FENRIR_ENABLE_RENDERER=OFF`.
