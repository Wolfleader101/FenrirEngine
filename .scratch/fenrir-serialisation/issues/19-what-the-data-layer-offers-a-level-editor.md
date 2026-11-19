# What must the scene format and engine API offer a level editor?

Type: grilling
Status: resolved
Blocked by: 05, 06, 13, 14

## Question

Reading the level editor research against the scene file and prefabs as decided, what does the engine carry natively, what does a tool bake into meshes and plain components, and what is left to the game?

- **Brush geometry:** whether brushes and CSG blockout live in the scene file (as planes or polygons the engine or a tool turns into meshes and colliders) or only in a tool's own file, with the scene holding the baked result; the engine/game line says baking is a tool's unless the research shows a reason otherwise.
- **Gameplay wiring:** whether an entity input/output or signal system (Source's) is an engine building block in the scene file, a game pattern over components and events, or out of scope.
- **Organisation:** layers, groups, visgroups and folders: in the scene file as entities or components, as editor-only data in the file, or not the engine's.
- **World data:** terrain, splines, scatter and navigation: which the scene file can hold as components and assets today, and which need a decision of their own (candidates for fog or new efforts).
- **Editor API:** which building blocks the engine exposes for a tool (the type registry for property panels, create, destroy and reparent at runtime, bounds and picking, saving a running scene, hot reload, a play-in-editor copy of a scene), and which are the tool's.
- **Units and precision:** metres, Y-up (the engine's conventions) and what a grid or snapping needs from the data.

The resolution names which items become specs on this map, which go to fog with a graduation condition, and which are out of scope. Zoom into [What makes a good level editor?](05-what-makes-a-good-level-editor.md), [What do level editors demand of a scene format and engine API?](06-what-level-editors-demand-of-the-data.md), [What does a scene file hold?](13-what-a-scene-file-holds.md) and [How do prefabs work?](14-prefabs.md).

## Answer

ADR: `docs/adr/0068-editor-data-rides-on-editor-only-components-and-brushes-wiring-and-world-data-stay-outside-the-engines-scene-file.md`. Glossary: Editor-only. One grilling round, every recommendation accepted.

- **Into the specs:** two additions only, folded into existing specs: an `@editor-only` doc command on a type (type-registry spec; the cooker strips those types) and `Assets::CopyScene(const Scene&, name) -> Scene&`, a play-in-editor copy through the writer and reader in memory (load/save spec). Everything else an editor needs is already decided: the type registry and metadata vocabulary (ADR-0060), run-time create, destroy, reparent, add and remove (the ECS), `Edit` for data assets (ADR-0065), scene and subtree save (ADR-0065, ADR-0066), hot reload with `Reloaded()` (ADR-0067), and the bridge that snapshots a component to a value tree and back for an undo stack.
- **Brush geometry:** out of the scene file; a tool bakes blockout to mesh assets plus the `MeshRenderer` and `Collider` components already written. Fog: Blockout tool.
- **Gameplay wiring:** a package's or the game's over pieces already in place (`entity` fields surviving instancing, events, `fixed-string`, `list`, `@formerly`); `@input`/`@output` doc commands may follow. Fog: Entity wiring.
- **Organisation:** ordinary described components and scene resources from an editor package, marked `@editor-only`, written like any other, ignored at run time, opaque where the package is absent, stripped when cooked. Fog: Editor organisation package. Run-time layers are the game's.
- **World data:** out of scope; terrain, splines, scatter, navigation and lighting bakes are packages with components and assets by path, bakes beside the scene; streaming partitions are a tool's computation over bounds and a flag (noted in the streaming fog entry).
- **Units and precision:** nothing from the file (ADR-0015's metres, Y-up, right-handed); grid and snap settings are project-level editor settings in the dev-time settings file (18).
- **Out of scope, named on the map:** world-data packages, picking, gizmos, per-entity bounds queries, undo, selection, copying play changes back, partial and cordoned builds, budget views. A level editor gets its own map when the effort begins; the research's finding that UX and iteration speed decide an editor's worth goes into the Notes for it.
