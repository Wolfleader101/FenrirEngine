# What do level editors demand of a scene format and engine API?

Type: research
Status: open
Blocked by: none

## Question

Read the same editors as [What makes a good level editor?](05-what-makes-a-good-level-editor.md) for their **data**: what their map and scene files hold, and what their engines expose so the editor can work, in the areas that constrain Fenrir's scene file and the building blocks the engine offers a tool: Source's `.vmf` and compiled BSP, Source 2's `.vmap` and its compile, TrenchBroom's `.map` with game definition files (FGD, `.def`), Unreal's `.umap`, Level Instances, Packed Level Actors and one-file-per-actor, Godot's `.tscn` as a level file, Unity's scenes with ProBuilder, Dunia, Forge, and Bevy's scene work.

- **Brush geometry:** how brushes, CSG and blockout geometry are stored (planes, polygons, half-spaces), when they are baked to meshes, and what the engine sees at runtime (meshes and colliders, or brushes themselves).
- **Entities and wiring:** how a map file names an entity's class and properties (FGD-style definitions the editor reads), and how gameplay wiring is stored (input/output connections, named targets, trigger volumes, logic entities).
- **Organisation in the file:** layers, visgroups, groups, folders, instances with overrides, cordons.
- **World data beyond entities:** terrain heightfields and layers, splines and roads, foliage and scatter, navigation meshes, lighting bake inputs and outputs, streaming cells and their bounds.
- **Engine API the editor needs:** a type and property registry to build property panels from, create/destroy/reparent entities at runtime, picking and bounds, snapping helpers, undo-friendly edits, saving a running scene, hot reload, and play-in-editor's scene copy or reset.
- **Compile steps:** what runs between the editor's file and the shipped level (BSP compile, lighting, navmesh, packing), and what the editor file keeps that the shipped one drops.

Write findings to a Markdown file on the `research/level-editor-data` branch (`.scratch/fenrir-serialisation/research/level-editor-data.md`), citing each claim's source and pinning the versions read, and link it here.
