# What does FenrirEngine give a game today?

Type: research
Status: resolved
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

## Answer

Full findings, with a file pointer for every claim, the complete list of 58 missing items and a "Not verified" section: `.scratch/flightless-frenzy/research/engine-inventory.md` on branch `research/flightless-frenzy-engine-inventory` (commit 55f1e1c6; never merged). Read from source and docs at c1f63237; nothing was built or run.

**What a game can use**

- **Running:** `App` with parallel and main-thread systems in phases around a fixed 1/60 tick; events and resources; scenes created, switched and destroyed by name, with inactive scenes not simulating.
- **Input:** polled keyboard, mouse and gamepads with correct edges per tick or frame; captured cursor for mouse look; UI consumes input first. All of it from C#.
- **Rendering:** static, instanced and skinned meshes; PBR and unlit materials; **a game's own material shaders** (GLSL, C++ registration only), which can read the sun, lights, shadows and sky, so banded toon shading is possible as a material; clustered lights, sun and local shadows, sky with image-based lighting; per-camera post-processing (bloom, GTAO, SSR, grading); culling and LODs.
- **Assets:** glTF/GLB, FBX, OBJ, PNG, JPEG, HDR, cubemaps, fonts, loaded synchronously.
- **Physics:** rigid bodies, five shapes, 32 collision layers, triggers with enter and exit events, contact events, ray and shape queries, joints, vehicles. `CharacterBody` is velocity-driven and handles slopes, stairs, floor snap, riding moving platforms and pushing bodies, and sets off triggers; gravity, jump, extra jumps, air control and acceleration are game code by design (ADR-0014).
- **Animation:** clips from glTF and FBX, crossfades, layers with bone masks, clip events, bone attachments. All from C#.
- **UI:** FenrirUI markup with a CSS subset or a code tree, on the window, in an image or on a world quad; label, button, checkbox, slider, progress, image, scroll and text entry; gamepad and tab focus; hot reload; Razor panels in C#. UIDemo already has a HUD and a pause menu.
- **Scripting:** ECS-first C# (components, events, systems by phase, queries), hot reload keeping component data. PhysicsDemo's C# scenes are wholly C#.
- **Debugging:** debug draw with text, physics debug categories, camera debug views, renderer statistics, logging.

**What is missing, as it bears on this game**

- **Building against the engine from outside it:** no install, export or package; the only route is a build that contains the engine source. The demo scaffold, which does package installs, asset copying and the script build, is example code. No engine-level function builds a script project. No distribution story or bundled .NET runtime.
- **Levels:** no scene, level or prefab format, no serialisation, no save data, no editor. Levels are code; the nearest authored path is a glTF spawned an entity per node, with collision built by hand. Collision from a model asset is out of scope in the physics map.
- **Time:** no time scale, pause, timers, tweens or random source.
- **Ragdolls are not built.** The spec and eight tickets are ready (`.scratch/fenrir-physics-ragdolls/`); the charting prompt assumed they existed.
- **Effects:** no particles, billboards, sprites, trails or decals.
- **Outlines:** material shaders cannot read scene depth or normals, there is no stencil, and game code cannot add a post-process or render pass. No toon material ships. No fog, no per-entity tint (both planned on the renderer map).
- **Animation:** no state machines, blend spaces or root motion, and no animation of plain entities (a spinning coin, a platform on an authored path). Repo assets have idle, walk and run clips only.
- **Enemies:** no navigation or path-finding.
- **Input:** no action or binding layer, rebinding, dead zones or rumble (out of scope in the README).
- **Camera:** no camera controller in the engine.
- **UI:** one monospace font; no dropdown, popups, modals, tabs or menu stack; no arrow-key focus navigation; no billboarded world UI.
- **From C#:** no scene create or switch, no `App.Stop`, no entity parenting, no custom shader registration, no renderer configuration, no C#-defined resources.
- **Window:** no fullscreen, borderless or resolution setting.
- **Audio:** none, and nothing plans it.
