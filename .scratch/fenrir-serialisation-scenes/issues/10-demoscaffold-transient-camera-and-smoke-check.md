# 10: DemoScaffold: `Transient` free-fly camera and the save-and-reload smoke check

**What to build:** every demo scene saves and loads back without loss, which is checked in the smoke run (the spec's "Demos").

- **DemoScaffold:** the free-fly camera entity, and any debug entity the scaffold makes, carry `Transient`.
- **The smoke check:** under `FENRIR_DEMO_SMOKE`, each demo scene is:
  1. saved with `SaveScene` to a temporary path;
  2. loaded back into a second scene with `LoadScene`;
  3. compared with the original by its counts of entities and written components, failing the smoke run on a mismatch.

  The temporary files are removed afterwards.
- The demos' spawn functions and scene lists stay code.

**Blocked by:** 06.

**Touches:** `examples/DemoScaffold/` (`src/DemoScaffold.cpp`, its headers)

**Status:** ready-for-agent

- [ ] The free-fly camera carries `Transient`, and no saved demo scene holds it.
- [ ] The smoke run saves and reloads every demo scene, and the counts match.
- [ ] A deliberately dropped component makes the smoke check fail, naming the scene.
