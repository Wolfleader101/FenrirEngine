---
status: proposed
---

# Large demo assets are downloaded at configure time, and tests never use them

The lighting demos need showcase models, scanned plants and rocks, and HDR panoramas: 100–200 MB that would bloat every clone, as the committed backpack and the old DemoApp textures already have (most of a 143 MB pack).

- **`FENRIR_FETCH_DEMO_ASSETS`**, a CMake option off by default, downloads each asset at configure time from a pinned upstream URL (Khronos glTF-Sample-Assets, Poly Haven, Solar System Scope), checks its SHA-256, and caches it outside the build directory, so clean builds do not download again. One manifest lists every file with its URL, hash and licence.
- **Only assets with a stable upstream are fetched.** The backpack has none, so it is removed rather than rehosted.
- **A scene whose assets are missing shows an on-screen message** saying which option fetches them, and is otherwise skipped; smoke runs pass without them.
- **Tests and smoke checks use committed assets only**, so CI never downloads. Small ones stay in the repo: Khronos's `BoxTextured` (replacing the backpack in RendererDemo, PhysicsDemo, ScriptingDemo and `SpawnModel`'s test), the PNG skyboxes, and one ~2 MB panorama for the sphere-grid scene.
- **History is rewritten once** to remove the large binaries already committed, as its own task before the lighting family's integration branch is cut.

## Considered options

- **Git LFS.** Rejected: a hosted quota, and every clone needs it installed.
- **Committing them.** Rejected: what the history rewrite exists to undo.
