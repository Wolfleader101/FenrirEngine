# What makes a good level editor?

Type: research
Status: open
Blocked by: none

## Question

What do level designers and modders value in the level and map editors they praise, beyond a general engine's scene view: Valve's Hammer (Source) and Source 2's Hammer, TrenchBroom (Quake), Unreal's level editor with Level Instances and World Partition, Far Cry's Dunia editor, Halo's Forge, Portal 2's Puzzle Maker, Bethesda's Creation Kit, Godot's and Unity's scene editors, Dreams, and whatever the design community (GDC talks, designer post-mortems, modding communities) holds up as the best tooling. Read what designers say, not only what the tools' docs claim.

- **Workflows:** greyboxing or blockout (brushes and CSG, primitives, kitbashing), placing props and items, wiring gameplay (Source's entity input/output, triggers, logic entities), composing open worlds (tiles, cells, terrain, roads and splines, foliage painting), authoring puzzles (constrained palettes, validation, play-in-editor), and the edit-play-edit loop (hot reload, in-game editing, cordons or partial builds).
- **Organisation:** layers, groups, visgroups, folders, instances and prefabs, references across maps, collaboration (one file per actor, locking, merge).
- **Precision:** grids and snapping, units, measuring, alignment and texture tooling, selection and gizmos that designers call good or bad.
- **Feedback:** what designers want to see live (lighting, navmesh, performance budgets, errors) and how the best tools surface it.
- **Anti-patterns:** what designers complain about (slow iteration, brittle references, merge conflicts, hidden state, tools that fight the engine's data model).

Write findings to a Markdown file on the `research/level-editors` branch (`.scratch/fenrir-serialisation/research/level-editors.md`), citing each claim's source and pinning the versions read, and link it here. Write them as what a tool must let a designer do, so that [What do level editors demand of a scene format and engine API?](06-what-level-editors-demand-of-the-data.md) and the grilling ticket that follows can read each want against Fenrir's data layer.
