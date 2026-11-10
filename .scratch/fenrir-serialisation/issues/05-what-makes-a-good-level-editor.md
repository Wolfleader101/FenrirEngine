# What makes a good level editor?

Type: research
Status: resolved
Blocked by: none

## Question

What do level designers and modders value in the level and map editors they praise, beyond a general engine's scene view: Valve's Hammer (Source) and Source 2's Hammer, TrenchBroom (Quake), Unreal's level editor with Level Instances and World Partition, Far Cry's Dunia editor, Halo's Forge, Portal 2's Puzzle Maker, Bethesda's Creation Kit, Godot's and Unity's scene editors, Dreams, and whatever the design community (GDC talks, designer post-mortems, modding communities) holds up as the best tooling. Read what designers say, not only what the tools' docs claim.

- **Workflows:** greyboxing or blockout (brushes and CSG, primitives, kitbashing), placing props and items, wiring gameplay (Source's entity input/output, triggers, logic entities), composing open worlds (tiles, cells, terrain, roads and splines, foliage painting), authoring puzzles (constrained palettes, validation, play-in-editor), and the edit-play-edit loop (hot reload, in-game editing, cordons or partial builds).
- **Organisation:** layers, groups, visgroups, folders, instances and prefabs, references across maps, collaboration (one file per actor, locking, merge).
- **Precision:** grids and snapping, units, measuring, alignment and texture tooling, selection and gizmos that designers call good or bad.
- **Feedback:** what designers want to see live (lighting, navmesh, performance budgets, errors) and how the best tools surface it.
- **Anti-patterns:** what designers complain about (slow iteration, brittle references, merge conflicts, hidden state, tools that fight the engine's data model).

Write findings to a Markdown file on the `research/level-editors` branch (`.scratch/fenrir-serialisation/research/level-editors.md`), citing each claim's source and pinning the versions read, and link it here. Write them as what a tool must let a designer do, so that [What do level editors demand of a scene format and engine API?](06-what-level-editors-demand-of-the-data.md) and the grilling ticket that follows can read each want against Fenrir's data layer.

## Answer

Findings: `.scratch/fenrir-serialisation/research/level-editors.md` on branch `research/level-editors` (commit `0ceac193`), against TrenchBroom v2026.2, Unreal 5.9, Unity 6.6, Godot 4.7, Valve's source-sdk-2013 (`vbsp/map.cpp`, `entityoutput.h`), Halo Forge (Season 4 and CU29 overviews), Portal 2's Puzzle Creator (Portal Wiki, BEE2.4), the Starfield Creation Kit wiki, Dreams Docs, Robin-Yann Storm's 2016 article, the Burgess and Purkeypile kit articles (GDC 2013 and 2014), Jim Brown's GDC 2010 tools slides and Robert Yang's Level Design Book. The Valve Developer Community and the Bethesda wikis sit behind bot checks, so Hammer and Creation Kit claims cite the SDK source and search excerpts; the file flags each.

- **Framing:** designers judge editors by their UX, not their geometry tech: Hammer's win is seeing geometry, its measurements and its position in one glance; one fixed UX issue saves twenty tool users an hour a day; "iteration is king"; gameplay wiring shapes behaviour more than walls do.
- **Blockout:** drag out a convex solid on a power-of-two grid in one gesture with dimensions visible; CSG (merge, subtract, intersect, hollow) with convexity validated; blockout geometry solid at once (Unreal users still batch-fix collision after the fact); grid sized to the player (Quake 32/64, Unreal 64/128, kits at module size); blockouts thrown away cheaply; UV lock and per-face UV axes so textures survive edits.
- **Kits and props:** footprints in multiples (512 room, 256 hall), snap at half the footprint, snap-to-reference turning any rotated piece into a new grid; pivots never change once content exists; kit pieces instanced so art edits show everywhere; a palette with persistent categories and recents; scaling instead of per-size assets.
- **Wiring:** per-connection target, input, parameter, delay and fire count (Source's `CEventAction`), shown from both ends, red when dangling, fireable from the console while playing; node graphs budgeted, logged and shareable as prefabs; instancing must fix up names and I/O targets per instance (VBSP does, TrenchBroom's linked groups do not, hence MESS).
- **Open worlds:** load a region, never the world; cells, streaming sources and HLODs set in the editor; cross-cell references load together and Level Blueprint references pin actors always-loaded (a streaming cost, not only a data one); splines that deform terrain non-destructively and carry meshes; foliage painting with density, randomisation and filters; procedural fill in the same editor but kept away from level design proper.
- **Puzzles:** a constrained palette whose items know legal placements and clearance; one-key connect, many-to-one receivers; one-key build-and-play with a visible budget bar; a one-way escape hatch to the full editor; the community's first demand was to extend the palette.
- **Edit-play loop:** play-in-editor in seconds with pause, eject and edit; explicit rules on which play-time edits survive; partial compiles (cordons, hidden groups excluded, region loads); reproducible compile profiles; leak and portal visualisation; unlimited transactional undo plus autosave.
- **Organisation:** hide, lock and isolate by hand-made and automatic groups, nested, with membership in several; editor-only layers separate from runtime layers gameplay toggles; in-place sub-scene instances with per-instance overrides and revert; stable move-safe ids (Godot `uid`, one-file-per-actor); no whole-file locks; layered plugins with unintended overrides made visible.
- **Precision:** power-of-two grid, snap on by default, per-operation snap with units in world, object or camera space; snap to another object; 2D orthographic views with measurements; vertex, edge, face and clip-plane editing; bulk edits without lag; selection that respects lock and hide and scales past 150 objects.
- **Feedback:** a live issue browser with quick fixes; budgets on screen while editing and playing with warnings at 80%; live lighting plus explicit build steps for lighting, navmesh, audio and reflections; navmesh generated then hand-cleaned; wiring and streaming debug views; watch the player, not the editor.
- **Anti-patterns:** laggy basic geometry; whole-file locks and binary merges; brittle references (irreversible instance breaks, dropped layer references, deleted references that crash, three instance search paths); hidden state (easy modified flags, discarded comments, prefabs silently becoming new assets); instancing that is really duplication; tools that fight the engine's data model; kit metrics changed after content exists; budgets eaten by organisation.

The file ends with an eleven-item "Data-facing wants at a glance" list for [What do level editors demand of a scene format and engine API?](06-what-level-editors-demand-of-the-data.md).
