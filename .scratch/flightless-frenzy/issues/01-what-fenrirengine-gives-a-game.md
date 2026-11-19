# What does FenrirEngine give a game today?

Type: research
Status: open
Blocked by: none

## Question

What does FenrirEngine, as it stands on `main`, offer to someone building a small 3D platformer on it, and what does it lack? Answer from the repo itself: `README.md`, `GLOSSARY.md`, `docs/adr/`, the public headers of every package under `packages/`, the C# side (`FenrirScripting`, the Razor panels), and the demos under `examples/` (what each one shows, and how much game-shaped code it has to write itself).

Cover, package by package, what a game can use and how it is reached from C++ and from C#:

- **Running a game:** the app, scheduler, time and fixed tick; scenes and switching between them; how a program outside `examples/` would build against the engine (CMake targets, presets, install or `add_subdirectory`, the C# solution).
- **Input:** keyboard, mouse (relative look, cursor capture), gamepad; any action or binding layer.
- **Rendering:** meshes, materials, lights, shadows, sky, post-processing, culling and LOD; custom shaders or materials (could a game do cel shading or outlines?); particles; transparent and unlit drawing; the camera.
- **Assets:** which model, texture and animation formats load; what an asset pipeline step looks like; whether assets load asynchronously.
- **Physics:** rigid bodies, shapes, triggers and contact events, collision layers, the character body (ground state, slopes, steps, moving platforms), queries (ray, shape casts), joints, vehicles, ragdolls.
- **Animation and skeletons:** clips, blending, state machines, root motion, events.
- **UI:** FenrirUI markup, stylesheets, controls, Razor panels, world-space UI, text; what a HUD and a menu would be built from.
- **Scripting:** what a C# game script looks like, hot reload, what is bound and what is not.
- **Levels and persistence:** any scene or prefab file format, serialisation, save data, an editor or level-authoring path.
- **Debugging:** debug draw, logging, in-game debug panels.
- **What is missing:** audio (known), and anything else a platformer normally needs that no package provides. For each, say whether any map, spec or ADR already plans it.

Give facts with file pointers, not recommendations.
