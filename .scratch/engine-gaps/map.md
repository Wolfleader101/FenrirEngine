# Engine gaps for Flightless Frenzy

Label: wayfinder:map

## Destination

Every one of the fifteen engine-gap issues in `issues/01` to `issues/15` has a decided outcome: a spec-ready design (ready for `/to-spec`), a pointer to an existing map or spec that covers it with anything missing added there, `wontfix` with a reason, or deferred with a reason. The gaps are grouped into efforts by package, ordered by what [Flightless Frenzy](../flightless-frenzy/map.md) needs first.

## Notes

- **Who asks:** Flightless Frenzy, a C# game on FenrirScripting with Razor panels and a C++ host that must not grow, built as its own CMake project that adds the engine with `add_subdirectory` and uses no demo code. Each gap issue says what the game needs; the game's tickets in `.scratch/flightless-frenzy/issues/` say it exactly. That folder is read here, never edited.
- **The game works around no gap and fakes no renderer feature** (the user, 2026-11-19). A design here is for the engine, not a stopgap for the game.
- **The engine/game line** (`GLOSSARY.md`, `docs/adr/`, ADR-0057's spirit): the engine provides building blocks; the game decides what the world is. A design that decides a game's world (which meshes are solid, what an enemy walks on, what a menu stack is) is on the wrong side.
- **Existing maps first.** Before designing a gap, check the map that might own it: `.scratch/fenrir-renderer/map.md` (fog, particles and per-entity overrides are lines in its Not yet specified), `.scratch/fenrir-physics/map.md` (a collision asset pipeline is out of its scope), and the unmerged `fenrir-serialisation` map on `claude/wayfinder-serializable-resources-246b15` (scene files, data assets, C# data; read with `git show`, prefix `MSYS_NO_PATHCONV=1` in Git Bash). The levels and scenes design runs in another session on that map: record dependencies on it, never redesign it here.
- **Inventory:** `.scratch/flightless-frenzy/research/engine-inventory.md` on `research/flightless-frenzy-engine-inventory`.
- **The renderer features are decided here** (the user, 2026-11-19): fog, particles, outlines and game passes, and per-entity material parameters. When each ticket resolves, its line in the renderer map's Not yet specified becomes a pointer to it, and its spec is written as `.scratch/fenrir-renderer-<name>/`, like the renderer map's other specs.
- **Gap statuses** (the user, 2026-11-19): a gap whose outcome is spec-ready or a pointer becomes `ready-for-agent`; a deferred gap becomes `needs-info`, its comment saying what brings it back; a ruled-out gap becomes `wontfix`. A gap with several decision tickets changes when the last resolves.
- **Each decision ticket names the gap issues it settles** on its `Settles:` line. When a ticket resolves, the gap issue gets a comment pointing at it, and its `Status:` changes as below.
- **Skills:** grilling tickets call `grilling` and `domain-modeling`; package and seam questions also use `codebase-design`. Research tickets call `research` and put their findings in `.scratch/engine-gaps/research/<name>.md` on a `research/<name>` branch, never merged. The task ticket is done on a throwaway project, never merged.
- **Plan, don't do:** tickets produce decisions, not code.

### The efforts

Each effort touches different packages, so the efforts are **independent and can run in parallel**; within one, tickets follow their `Blocked by:` lines. Ordered by what the game needs first:

| Order | Effort | Packages | Gaps | Tickets | The game needs it |
| --- | --- | --- | --- | --- | --- |
| 1 | **Build helpers** | root `CMakeLists.txt`, `cmake/`, `examples/DemoScaffold/cmake/` | 02 | 16 → 17 | before its first ticket |
| 2 | **C# reach** | FenrirScripting, FenrirScene, FenrirApp, FenrirAssets bindings | 11, 13, 04 (positions) | 18, 19, 20, 33 | parenting early; scenes for restart and title |
| 3 | **Pause and time scale** | FenrirTime, FenrirApp, FenrirPhysics, FenrirAnimation | 05 | 21 | for the pause menu |
| 4 | **Renderer features** | FenrirRenderer, FenrirAssets materials | 06, 07, 14, 15 | 22 → 23, 24 → 25, 26, 27 | before the game is complete |
| 5 | **Wanted, not blocking** | FenrirInput, FenrirUI, FenrirWindow | 08, 09, 10, 12 | 29, 30, 31, 32 | never blocks |
| – | **Waits on the levels design** | FenrirAssets, FenrirPhysics | 04 (the helper) | 28 | once levels are authored |
| – | **Covered elsewhere or deferred** | | 01, 03 | none | see below |

Within the renderer features, the four tickets are independent of each other as decisions; outlines and particles may both ask the frame for scene depth, which their tickets note.

### Outcomes without a ticket

- [The engine has no audio](issues/01-audio.md): **deferred** by the user; the game is planned silent.
- [No scene or prefab files, and no way to save data](issues/03-scene-files-and-save-data.md): **a pointer** to the `fenrir-serialisation` map, which plans scene files, prefabs, data assets, load and save, and C# data (its ticket 16 covers C# loading scene files); save games are the game's own file there (ADR-0064), as the game already decided. Nothing is added to that map (the user, 2026-11-19); that the glTF importer drops node `extras` went to the levels and scenes design through the sessions' shared notes.

## Decisions so far

<!-- one line per resolved ticket: [title](issues/NN-slug.md): gist -->

- [What breaks when the engine is built as a subdirectory?](issues/16-build-the-engine-as-a-subdirectory.md): two plain variables (`IRIS_BRIDGE_DIR`, `FENRIR_SHADER_INCLUDE_DIR`) never reach the game and stop scripts and shaders; the engine's C++20, warnings and compile commands stay in its own scope; a nested build tree passes MSVC's path limit; the deferred checks work; C# needs the built SDK's props and a contract path per contract.
- [Does C# get a loaded mesh's positions?](issues/33-does-csharp-get-a-loaded-meshs-positions.md): yes, `Assets.GetMeshPositions` into a span, shaped like `GetMeshLodIndices`, in the C# reach effort; spec-ready.
- [Does the engine offer input actions or dead zones?](issues/29-does-the-engine-offer-input-actions.md): radial per-stick dead zones in FenrirInput, settable, raw values still readable, spec-ready; the action layer deferred until a game wants rebinding.
- [Do arrow keys move UI focus?](issues/30-do-arrow-keys-move-ui-focus.md): arrow keys always drive directional navigation after the focused element's refusal; W and S are the game's through a public call that sends a direction; spec-ready.
- [What do fullscreen and display settings look like?](issues/31-what-fullscreen-and-display-settings-are.md): windowed and borderless fullscreen on the current display, from C++ and C#, spec-ready; exclusive modes and display lists deferred; MSAA stays fixed.
- [Does the engine offer navigation?](issues/32-does-the-engine-offer-navigation.md): deferred; a navigation mesh from given geometry is an engine block, but no game asks yet.
- [How do engines split particles between the engine and the game?](issues/24-how-engines-split-particles.md): every engine owns the runtime and a behaviour vocabulary and the game owns the effect as data, played by one call; they differ on CPU or GPU, vocabulary size and collision; FenrirEngine has only instance lists, no camera-facing quad, no soft-particle depth and no pass a package can add.
- [How do engines let a game add a render pass and draw outlines?](issues/22-how-engines-add-render-passes-and-outlines.md): every engine runs a game callback at named frame points reading the frame's depth and normals, and outlines chosen objects by layer, flag or material pass; FenrirRenderer makes scene depth and normals already but exposes no pass, no front-face cull, no stencil and no per-object mask.

## Not yet specified

- **Spec boundaries:** how the spec-ready designs become specs: one spec per effort, or per ticket (fog and per-entity parameters may be small enough to share one renderer spec; parenting and scenes may share one C# spec). Settled once each effort's tickets close.
- **Which ADRs:** pause and time scale and the frame's game passes look hard to reverse; each ticket says whether it is one.
- **Collision from a model** stays in its ticket's waiting state until the levels and scenes design says how a level's geometry and collision are authored; it may graduate into a pointer to that map instead.
- **Volumetric light:** the renderer map's shafts and froxels, past the plain fog the game needs; whether it is named as a later spec there comes out of "What fog does the renderer offer?".
- **Gaps a later game ticket raises:** the game's unresolved tickets ("How does Kiki move?", "What does the player play with?", "What are the enemies?", the toon-look prototype, "How are levels authored and loaded?") may raise new engine gaps; each arrives as a new issue here, `needs-triage`, and is charted then.

## Out of scope

- **The game itself:** anything in `.scratch/flightless-frenzy/` and `games/FlightlessFrenzy/`.
- **Redesigning scene files, prefabs, data assets or save data:** the `fenrir-serialisation` map's.
- **Lifting the demo UI kit into the engine:** ADR-0071 keeps it in the scaffold, and the game wants none of it; only the build helpers move.
- **Install, export and packaging of the engine:** the game adds the engine with `add_subdirectory` ([How does the game build against the engine?](../flightless-frenzy/issues/03-how-the-game-builds-against-the-engine.md)).
- **C++-only reach the game keeps in its host:** registering material shaders and renderer configuration from C#.
