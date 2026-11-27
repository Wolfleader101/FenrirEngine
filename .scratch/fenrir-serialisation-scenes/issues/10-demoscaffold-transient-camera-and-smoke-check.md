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

**Touches:** `examples/DemoScaffold/` (`src/DemoScaffold.cpp`, its headers); as built, also `packages/FenrirSerialisation/` (the union tag) and `packages/FenrirAssets/` (the scene reader), below

**Status:** resolved

- [x] The free-fly camera carries `Transient`, and no saved demo scene holds it.
- [x] The smoke run saves and reloads every demo scene, and the counts match.
- [x] A deliberately dropped component makes the smoke check fail, naming the scene.

## Comments

- **How the counts are compared:** the scene loaded back is saved too, and the two saves' entities and components written are compared, so both sides count what a file holds the same way. An instance is one block on each side, though its load expands it. A component kept opaque is one entry on each side. Within one app, the writer writes only types the same registry describes, so a reload cannot turn a live component opaque. `RoundTripMismatch` (`src/DemoRoundTrip.hpp`, a seam kept out of the scaffold's public header) makes the comparison and words the error. Its test feeds it a dropped component, since no real round trip can be made to drop one on purpose.
- **When:** after each smoke scene's frames, before the next scene is entered, so a scene's run-time entities are in the file too. The files go under a `.demo-smoke-<random>` folder of the asset root, since two CTest smoke runs of one app share it.
- **Two engine faults the check found, fixed here (the user, 2026-11-27):**
  - **The tag spelling:** FenrirSerialisation's `FromTree` looked for a tagged union's tag under the tag field's own name (`m_type` in every engine union), while `ToTree` writes `kind`. So `Sky`, the physics shapes and `Camera::projection` lost their alternative on load, with an error. Both now use one key, `Detail::UnionTagKey`, with a test over an `m_type`-tagged union.
  - **The Transform gap:** AnimationDemo's "No Transform" bone attachment has no `Transform`, which the reader's `CompleteEntity` gave back. A block now loads with only the Name and Transform it lists, since the writer lists both, as `{}` at their defaults, whenever the entity has them. This amends ADR-0078 §5 and the spec's "Loading and saving", with a test in `Tests/Scenes.cpp`.
- **A watch on Name or Transform:** for a block listing neither, `CompleteEntity` adds the pair and the reader then removes the one left out. So a watch drained after the load sees an add and a remove there, not nothing. A reader that skipped adding them would need a `CompleteEntity` without its defaults.
- **Left for later:** `Assets::Move` (`packages/FenrirAssets/src/Move.cpp`, ticket 11) still finds a union's tag by the tag field's name. So a moved asset named inside a tagged union is not rewritten. It needs the same `kind` key.
