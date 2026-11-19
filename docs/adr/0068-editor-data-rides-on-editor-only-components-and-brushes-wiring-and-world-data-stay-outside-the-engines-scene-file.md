---
status: accepted
---

# Editor data rides on editor-only components, and brushes, wiring and world data stay outside the engine's scene file

The user wants map-making tools in the spirit of Hammer and TrenchBroom, and asked what the data layer must offer one. The research found designers judge editors by UX and iteration speed, not geometry tech; that every engine read either compiles brushes away or bakes them to meshes and documents that as the path; that organisation (layers, groups, visgroups, cordons) is editor-only data TrenchBroom keeps as ordinary entities with reserved keys; that wiring is a string on the sender plus a name resolver; that terrain, scatter, navmesh and lighting bakes are assets beside the scene referenced by id; that streaming cells are computed by a tool from per-entity bounds; and that the editor's property registry is the same description the loader uses everywhere but Source, whose hand-written FGD drifts. Fenrir's conventions are fixed (metres, Y-up, right-handed, ADR-0015), the ECS gives create, destroy, reparent, add and remove at run time, and ADR-0064 keeps unknown components opaque.

We decided that **the scene file carries editor data only as ordinary components marked editor-only, and that brushes, wiring and world data are packages or tools outside the engine**:

1. **Brush geometry stays out.** Blockout lives in a tool's own file or a future brush package's component and bakes to mesh assets under the asset root plus the `MeshRenderer` and `Collider` components the scene file already writes. If brushes ever enter a Fenrir file, the precedent is three plane points per side with material and texture axes, vertices derived.
2. **Wiring is a package's or the game's.** The pieces a Source-style input/output system needs are in place: `entity` fields written as ids that survive instancing through slash paths (ADR-0066), described events, `fixed-string` and `list` kinds, `@formerly` renames. A wiring component and the system that fires it are not the engine's; the description may later grow `@input` and `@output` doc commands so an editor shows connectable ends.
3. **Editor organisation is ordinary components, marked `@editor-only`.** Layers, groups, folded state, visgroups and cordons are described components and scene resources an editor package defines, written like any other, ignored by run-time systems, and kept opaque by ADR-0064 where the package is absent. The type description gains an `@editor-only` doc command; the cooker strips those types. Run-time layers gameplay toggles are a different thing and the game's.
4. **World data needs nothing new.** Terrain, splines, scatter, navigation and lighting bakes are each a package with components and asset types referenced by asset path, bake outputs as assets beside the scene so the scene file is stable under rebakes. Streaming partitions are a tool's computation over per-entity bounds and a flag, never a scene format feature.
5. **The editor's building blocks are the ones already decided, plus one.** The type registry and its metadata vocabulary (ADR-0060), run-time structural edits (the ECS), `Edit` for data assets (ADR-0065), scene and subtree save (ADR-0065, ADR-0066), hot reload with `Reloaded()` (ADR-0067), and the bridge that snapshots a described component to a value tree and restores it, which an undo stack records. Added: `Assets::CopyScene(const Scene&, name) -> Scene&`, a play-in-editor copy through the writer and reader in memory with no disk.
6. **Units ask nothing of the file.** A grid and snapping are arithmetic over `Transform`; grid size, snap and units are project-level editor settings in the dev-time settings file, not scene data.
7. **Out of scope, by name.** World-data packages, picking, gizmos, per-entity bounds queries, the undo stack, selection, copying chosen play changes back, partial and cordoned builds, budget views. A level editor gets its own map when the effort begins.

## Considered options

- **Brushes as a scene file primitive the engine renders and collides.** Godot's CSG and Unity's ProBuilder ship it to the runtime, and both document baking it away as the thing to do; the engine would carry a CSG rebuild for a blockout stage designers throw away.
- **A file-level editor section** (Hammer's `visgroups`, `cordons`, `editor{}` blocks). A second schema the runtime must skip and the merge driver must learn; components already have both, for free.
- **An engine wiring system in the scene file.** Source's is beloved, but it is one game's pattern over a name resolver; the engine offers the resolver and the ids, and a package can offer the pattern.
- **Engine-owned terrain, splines, scatter and navigation types now.** Godot has no terrain node and its navmesh is a resource baked from whatever geometry exists; each is a package when a game needs it, and the data layer carries whatever that package describes.
- **A separate editor spec on this map.** Two small additions do not make a spec; they fold into the type-registry and load/save specs, and the editor effort maps itself.

## Consequences

- `tools/describe-api` gains `@editor-only`; the cooked-form ticket decides how it strips those types; `FenrirAssets` gains `CopyScene`.
- The serialisation map's fog gains Blockout tool, Entity wiring and Editor organisation package, each graduating when a level editor effort begins; its Notes record that iteration speed and UX, not geometry tech, decide an editor's worth.
- An editor package's components in a scene file load as unknown components in a game build until the cooker strips them; a developer build with the package links them as ordinary data.
