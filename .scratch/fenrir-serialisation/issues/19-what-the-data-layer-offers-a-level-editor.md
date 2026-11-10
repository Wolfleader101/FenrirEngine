# What must the scene format and engine API offer a level editor?

Type: grilling
Status: open
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
