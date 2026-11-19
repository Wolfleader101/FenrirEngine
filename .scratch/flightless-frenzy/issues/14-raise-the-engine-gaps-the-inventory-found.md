# Raise the engine gaps the inventory found

Type: task
Status: resolved
Blocked by: 01

## Question

For each item in the inventory's "Missing for a platformer" list that this game, as scoped on the map, would need: raise one issue under `.scratch/engine-gaps/issues/` (`Status: needs-triage`), saying what the game needs, why the engine lacks it, whether anything already plans it, and whether it blocks the game or can be stubbed. Audio is already raised. Do not raise gaps the scoped game does not need.

The answer lists the issues raised, sorted into blocking and stubbable. Later tickets add their own gaps to the same folder as they find them.

## Answer

Eleven issues raised under `.scratch/engine-gaps/issues/`, beside the audio one, all `needs-triage`. None stops a first build; two block part of the game.

**Blocks part of the game**

- [A game outside `examples/` has no supported way to build against the engine](../../engine-gaps/issues/02-building-a-game-against-the-engine.md): the game can join the root build and write its own start-up, but cannot use only public engine pieces or move to its own repo.
- [No pause and no time scale](../../engine-gaps/issues/05-pause-and-time-scale.md): blocks a pause menu that stops physics and animation.

**Blocks only a C# game, in part**

- [C# cannot switch scenes, stop the app, parent entities or add a shader](../../engine-gaps/issues/11-what-csharp-cannot-reach.md): a C++ host can stand in for all but parenting.

**Can be stubbed**

- [No scene or prefab files, and no way to save data](../../engine-gaps/issues/03-scene-files-and-save-data.md): already being planned on the unmerged `fenrir-serialisation` map (branch `claude/wayfinder-serializable-resources-246b15`), which the inventory missed.
- [A level model does not give the level its collision](../../engine-gaps/issues/04-collision-from-a-model.md): ruled out of scope once on the physics map; raised as a smaller ask.
- [No particles or billboards](../../engine-gaps/issues/06-particles-and-billboards.md)
- [A game cannot draw outlines or add a render pass](../../engine-gaps/issues/07-outlines-and-game-render-passes.md): banded toon shading is possible today as a game material.
- [No navigation or path-finding](../../engine-gaps/issues/08-navigation.md)
- [No input actions or bindings](../../engine-gaps/issues/09-input-actions.md): the FenrirInput README rules this out for now.
- [Arrow keys do not move focus in FenrirUI](../../engine-gaps/issues/10-arrow-key-focus-in-ui.md)
- [No fullscreen or display settings](../../engine-gaps/issues/12-fullscreen-and-display-settings.md)

**Not raised**

- *Already planned on the renderer map, and the game can wait:* fog, per-entity material tint, animation state machines and animation of plain entities, asynchronous loading.
- *The game's own code by the engine's design:* gravity, jumping and air control on the character body (ADR-0014), the camera controller, pausing behind a menu as a rule of the game, a menu stack.
- *Assets, not engine:* a proportional font, jump and fall clips.
- *Not needed by this game:* ragdolls (the design document has none), trigger stay events, a second graphics backend, an editor.
- *Left in the map's fog:* packaging and bundling the .NET runtime.
