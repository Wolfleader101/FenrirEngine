# C# reach: parenting, scenes, the app, C# resources and mesh positions

Status: ready-for-agent

Charted by [Engine gaps for Flightless Frenzy](../engine-gaps/map.md): [How does C# parent entities?](../engine-gaps/issues/18-how-csharp-parents-entities.md), [How does C# create, switch and destroy scenes, and stop the app?](../engine-gaps/issues/19-how-csharp-reaches-scenes-and-the-app.md), [How does C# define its own resources?](../engine-gaps/issues/20-how-csharp-defines-resources.md) and [Does C# get a loaded mesh's positions?](../engine-gaps/issues/33-does-csharp-get-a-loaded-meshs-positions.md). Settles [C# cannot switch scenes, stop the app or parent entities](../engine-gaps/issues/11-what-csharp-cannot-reach.md), [C# cannot define resources](../engine-gaps/issues/13-csharp-defined-resources.md) and the reach half of [A level model does not give the level its collision](../engine-gaps/issues/04-collision-from-a-model.md).

## Problem Statement

A game written in C# on FenrirScripting, with a C++ host that only starts the app, cannot do several things C++ can:

- It cannot make one entity the child of another: `Relationship` is read-only from C#, and no call re-parents. A cage cannot hold a friend, an effect cannot follow the player, a camera rig cannot be built.
- It cannot create, switch or destroy scenes, so it cannot keep a title screen and a level apart, or restart a level by recreating its scene (which is how its physics world resets).
- It cannot stop the app (only close the window), and it reads only the frame's delta time and the tick rate, not elapsed time or the frame and tick counts.
- It cannot define state of its own that belongs to a scene or the app rather than an entity: a resource needs a native extension, so a run's timer, score and state live awkwardly as components on one "run" entity.
- It cannot read a loaded mesh's positions, so although it can cook a collision mesh from vertices it is given, it cannot make a loaded model solid.

Flightless Frenzy's rule is that a reach gap is fixed in the engine, not in its C++ host, which must not grow.

## Solution

C# gains, through the existing generated binding pipeline and facades: a writable `Parent` on an entity, applied as a deferred change; a `Scenes` facade to create, activate and destroy scenes by name, applied at the end of the frame in the order asked; `App.Stop` and read-only elapsed time and frame and tick counts; scene resources and app resources defined by a C# script assembly as blittable structs, admitted like C# components and read through the existing `Resources`; and `Assets.GetMeshPositions`, shaped like the existing mesh LOD readers.

## User Stories

1. As a C# game developer, I want to set an entity's `Parent` to another entity, so that the child moves with it.
2. As a C# game developer, I want re-parenting to keep the child where it is in the world by default, so that picking something up does not make it jump.
3. As a C# game developer, I want to re-parent keeping the child's local transform instead when I ask, so that I can snap something to a parent's origin.
4. As a C# game developer, I want to clear an entity's parent by setting it to no entity, so that a freed friend leaves its cage.
5. As a C# game developer, I want a parent change to land when my system ends, like inserts and removes, so that I can re-parent inside a query without breaking it.
6. As a C# game developer, I want to re-parent entities that spawning a model made, so that a spawned model can go under a game entity.
7. As a C# game developer, I want destroying a parent from C# to treat its children as C++ does, so that the rules are the same in both languages.
8. As a C# game developer, I want to read an entity's parent, so that I can find which cage holds a friend.
9. As a C# game developer, I want to create a scene by name, so that my title screen and my level are separate scenes.
10. As a C# game developer, I want to make a scene active by name, so that I can move from the title screen to the level.
11. As a C# game developer, I want to destroy a scene by name, so that a finished level frees its entities and its physics world.
12. As a C# game developer, I want to restart a level in one frame by creating a fresh scene, making it active and destroying the old one, so that the restart has no blank frame.
13. As a C# game developer, I want scene requests applied at the end of the frame in the order I made them, so that systems mid-frame never see a scene disappear under them.
14. As a C# game developer, I want a request to destroy the active scene refused with a logged error, as C++ refuses it, so that I am told to switch first.
15. As a C# game developer, I want a new scene to have every installed package's per-scene state, so that its physics world, renderer and UI state are there without my asking.
16. As a C# game developer, I want to read the active scene's name, so that my systems know whether they run on the title screen or in the level.
17. As a C# game developer, I want my script assembly's systems to run against whichever scene is active, as every package's do, so that one assembly serves every scene.
18. As a C# game developer, I want to stop the app from C#, so that my title screen's Quit button ends the program.
19. As a C# game developer, I want to read the time since the app started and the frame and tick counts, so that I can time things without keeping my own counters.
20. As a C# game developer, I want to mark a blittable struct as a scene resource, so that a run's timer, score and state belong to the level's scene.
21. As a C# game developer, I want a scene resource made with each scene and gone with it, so that restarting a level resets the run for free.
22. As a C# game developer, I want to mark a blittable struct as an app resource, so that settings and records outlive scenes.
23. As a C# game developer, I want to read and write my resources through the same `Resources` calls I use for native ones, so that there is one way to reach a resource.
24. As a C# game developer, I want a resource whose shape changed on a script reload reset to its defaults with a warning, so that hot reload keeps working while I change its fields.
25. As a C# game developer, I want a clear error when a resource type is not blittable, so that I know why it was refused.
26. As a C# game developer, I want to read a loaded mesh's positions into a span, with a way to learn how many there are, so that I can cook its collision with the indices I can already read.
27. As a C# game developer, I want reading positions from a mesh whose CPU copy is gone reported as unreadable, so that I am not handed nothing silently.
28. As the Flightless Frenzy developer, I want all of this in C#, so that my C++ host stays a host and holds no gameplay.
29. As an engine maintainer, I want these bindings made through the API description and generated bindings with hand-written facades only where a description is not enough, so that C# reach stays one pipeline.
30. As an engine maintainer, I want ScriptingDemo to show a parented C# entity and a C#-defined resource, so that I can check them by hand.

## Implementation Decisions

- **One pipeline.** Every new C# call is a native function described for the API description and generated into the bindings, with hand-written facade parts only where a description is not enough (ADR-0042, ADR-0043). The core calls (entities, scenes, the app, time, resources) live in FenrirScripting's own bindings; mesh positions live in FenrirAssets' scripting extension.
- **Parenting.**
  - `Entity.Parent` reads the entity's parent (or no entity) and setting it re-parents keeping the world transform; setting it to no entity unparents, keeping the world transform. `Entity.SetParent(other, keepWorldTransform)` chooses.
  - It is a deferred change on the call context, applied when the system ends in the order recorded, beside inserts and removes (ADR-0029), since a first parent adds a `Relationship`. `Relationship` stays a read-only component in C#.
  - Applying it uses FenrirECS's existing child add and remove, and, to keep the world transform, recomputes the local transform from the old world transform and the new parent's.
  - Re-parenting under one's own descendant, or under an entity in another scene, is refused with a logged error.
  - A destroyed parent's children follow the C++ behaviour, which the spec's tests pin down.
- **Scenes.**
  - A `Scenes` facade: `Create(name)`, `SetActive(name)`, `Destroy(name)` and `Active`, by name, as `App::CreateScene`, `ChangeActiveScene` and `DestroyScene` are in C++.
  - The app gains **scene requests**: create, activate and destroy, queued and applied at the end of the frame, after every phase, in the order made. C# uses them; C++'s immediate calls stay as they are, and C++ may use the requests too.
  - A request to destroy the active scene, applied at its turn, is refused with a logged error as the immediate call is; a restart's create, activate, destroy succeeds because the activate lands first. Creating a name that exists, or activating or destroying one that does not, is a logged error.
  - Every installed package sets up its per-scene state for a scene made this way exactly as for one made from C++; the spec confirms each package does so and fixes any that only set up the scenes that existed at install.
  - Script systems run against the active scene, as today.
  - The API leaves room for a scene loaded from a file to arrive through the same create path, which is the `fenrir-serialisation` map's.
- **The app and time.** `App.Stop()` stops the app after the current frame, as `App::Stop` does. `Time.Elapsed` (seconds since the app started, as a double), `Time.FrameCount` and `Time.TickCount` join `Time.DeltaTime` and `Time.TickRate`, all read-only.
- **C#-defined resources.**
  - A blittable C# struct marked `[SceneResource]` or `[AppResource]` is a resource of the script assembly. The script report lists it with its layout beside C# components, and admission checks it; a struct that is not blittable is refused at admission with an error naming it.
  - Its memory is native, laid out from the report as C# components' are.
  - An app resource is made, default-initialised, when its assembly is admitted. A scene resource is made, default-initialised, in every existing scene at admission and in each new scene, and is ended with its scene.
  - On a reload, a resource whose layout is unchanged keeps its value; one whose layout changed is reset to its defaults, with a warning naming it; one whose type is gone is removed.
  - `Resources.Get<T>`, `TryGet`, `GetReadOnly`, `Insert` and `Remove` reach it as they reach native resources. A C# resource is not reachable from C++ by type.
  - `Res<T>` system parameters, and describing a C# resource for scene files, are not in this spec.
- **Mesh positions.** `Assets.GetMeshPositions(mesh, span)` copies the mesh's CPU-side positions into a span of `Vector3` and returns how many it wrote, with a count query to size the span, in the shape of `GetMeshLodIndices` and `GetMeshLodErrors`. Level 0 indices already cross through `GetMeshLodIndices`, so the two feed `PhysicsWorld.CreateCollisionMesh`. A mesh whose CPU copy is absent is reported unreadable through the call's result, not as zero positions.
- **Glossary.** Scene resource and app resource already allow a script assembly as their keeper. *Scene request* is added: a create, activate or destroy of a scene, queued and applied at the end of the frame.

## Testing Decisions

- **A good test here drives a headless app, calls C# through the script fixture assembly, and checks what the engine then holds**: an entity's parent and world transform, which scene is active, which scenes exist, a resource's value, the positions read. It never checks how a binding is written.
- **FenrirScripting's Catch2 tests** (prior art: the resource, admission and reload tests, with the fixture assembly and the headless app):
  - parenting: world transform kept by default and local transform on request; the change landing when the system ends and not before; unparenting; re-parenting a spawned model's entities; refusals for a descendant and another scene's entity; a destroyed parent's children;
  - scenes: create, activate and destroy landing at the end of the frame in order; a restart in one frame; destroying the active scene refused; errors for bad names; a new scene having every installed package's per-scene state (at least physics where it is built);
  - the app and time: `App.Stop` ending the run after the frame; elapsed time and counts matching the app's;
  - C# resources: admission of scene and app resources, refusal of a non-blittable one; a scene resource in existing and new scenes and gone with its scene; values kept across a reload with an unchanged layout, reset with a warning when it changed, removed when the type is gone; `Resources` reaching them.
- **FenrirAssetsScripting's tests** (prior art: the mesh LOD tests): positions match the loaded mesh; the count query sizes the span; an unreadable mesh is reported as such.
- **ScriptingDemo, by hand,** gains two rows in its showcase: a C# entity parented under a box, following it as it bobs, and a C#-defined resource counting something shown on screen. Scene switching from C# is not demoed there, because the demo scaffold owns the scene list; the tests cover it. The existing smoke run keeps checking that its scripts load.

## Out of Scope

- Loading and saving scene files, and spawning prefabs, from C#: the `fenrir-serialisation` map's.
- Several scenes simulating at once, and per-scene script sets.
- `Res<T>` system parameters.
- Describing C# resources in scene files.
- A model-to-collider helper: it waits on the levels and scenes design ([Does a loaded model give C# its collision?](../engine-gaps/issues/28-does-a-loaded-model-give-csharp-its-collision.md)).
- Registering material shaders or renderer configuration from C#.
- Pause and time scale: their own spec.

## Further Notes

- The facts each decision rests on, with file and line, are in the engine-gaps tickets this spec names.
- Bindings and API descriptions are committed; regenerate them with the describe-api target, since CI fails on a stale description.
- Build and test only `msvc-x64-debug` locally, through the MSVC script and, in a worktree, the short-path junction (`README.md`, "Building and testing").
