# 05: Scenes from C#

Spec: [C# reach: parenting, scenes, the app, C# resources and mesh positions](../spec.md).

**What to build:** A C# script creates, activates and destroys scenes by name through a `Scenes` facade (`Create`, `SetActive`, `Destroy`, `Active`) over the app's scene requests, so a game keeps its title screen and level apart and restarts a level by recreating its scene. Every installed package sets up its per-scene state for a scene made this way exactly as for one made from C++: check each package that keeps per-scene state and fix any that only set up the scenes that existed at install. Script systems keep running against the active scene.

**Blocked by:** 04
**Touches:** `packages/FenrirScripting/src/`, `packages/FenrirScripting/api/`, `packages/FenrirScripting/Managed/Fenrir.Scripting/`, `packages/FenrirScripting/Tests/`, and the install code of any package that sets up per-scene state only at install

**Status:** ready-for-agent

Stacked on 03, and on 04's branch merged in.

- [ ] Tests through the script fixture: create, activate and destroy from C# land at the end of the frame in order; a restart in one frame works; destroying the active scene is refused.
- [ ] A test checks that a scene created from C# after install has physics's per-scene state when physics is built, and the same for each other package that keeps per-scene state.
- [ ] `Scenes.Active` names the active scene.
- [ ] The API description and generated bindings are current.
