# 02: DemoApp scene switcher

**What to build:** The DemoApp switches scenes by feature family, one F-key per family (spec: "DemoApp: scene switching"). `DemoScenes`, a DemoApp switcher and not engine code, replaces `SwitchOnF3`. Each scene registers its name, its family key, a one-line key help logged on entry, and optionally a key sequence for smoke runs. A family's key steps through that family's scenes and back to Default; another family's key jumps to its first scene; a key steps only through scenes registered for it. F3 keeps today's Default → Physics (C#) → Default. F4–F8 are reserved (joints, character controller, vehicles, ragdolls, soft bodies).

This is the prefactor the joints scenes build on. No joints scene lands here.

- The F2 debug-line mode becomes app-wide and is re-applied to each newly active world, fixing today's desync between the logged mode and the drawn lines.
- `FENRIR_DEMO_SCENE=<name>` starts in that scene. With `FENRIR_DEMO_KEYS=1` the switcher presses the active scene's registered keys as real `KeyboardKeyEvent`s, one a second after two seconds of settling, so C++ and C# readers both see them.
- Each scene reads its keys in every scene and acts only while active.
- `CSharpScene { int Scene; int Built; }` replaces `CSharpPhysicsScene`, registered once in the demo's scripting extension; `Playground.cs` builds where it finds its own id unbuilt.

**Blocked by:** None (can start immediately).

**Status:** resolved

- [ ] F3 steps Default → Physics (C#) → Default as before, logging each scene's key help on entry; with scripting off F3 does nothing (checked by hand).
- [ ] The F2 mode carries across a scene switch: the drawn lines always match the logged mode (checked by hand).
- [ ] A scene's keys do nothing while another scene is active.
- [x] `FENRIR_DEMO_SCENE=<name>` starts in that scene; with `FENRIR_DEMO_KEYS=1` it presses that scene's registered keys, seen by both C++ and C# readers, with no errors in Debug and Release.
- [x] `CSharpPhysicsScene` is gone; `Playground.cs` builds from `CSharpScene`.

## Answer

Built as specified, in `examples/DemoApp/src/DemoScenes.hpp` (the switcher), `src/CSharpScenes.hpp` (C#-built scenes and their ids, replacing `CSharpPhysicsScene.hpp`) and `scripts/CSharpScene.cs`. `CSharpScene` is registered in `ScriptingDemo::Extension()`.

- **F2:** the mode stays in `PhysicsDemo` (the app's one F2 reader). Each frame it sets the active world's categories when they differ from the mode, so a world is brought in line on its first frame active, and F2 pressed in a scene with no world still moves the mode on.
- **Home scene:** Default is the switcher's home, not a registered scene. Its entry line lists the registered scenes by F-key, so it shows only what this build has.
- **Smoke settling:** a frame counts as at most 0.1 s towards the two seconds, as the first frames' deltas include loading. Without that, `J` was pressed before C# had built the scene.
- **Checked by running** (clang Debug and Release): `FENRIR_DEMO_SCENE="Physics (C#)" FENRIR_DEMO_KEYS=1` starts there, logs its key help and presses `J` two seconds in, which C# reads (`J: kicked 15 bodies.`), with no errors or warnings. `FENRIR_DEMO_AUTOPLAY=1` steps F3 Default → Physics (C#) → Default → Physics (C#), logging each scene's key help.
- **Left for the hand check:** that F2's lines match the logged mode across a switch, that `J` does nothing in Default, and that F3 does nothing with scripting off (no scene registers for it then).
