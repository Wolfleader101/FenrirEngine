# 02: Meshes carry an optional tangent stream, and `lit` uses it

**What to build:** The importer keeps glTF's tangents (MikkTSpace) and FBX's when present, `MeshData` holds them as an optional stream, and `builtin:lit` uses them instead of the derivative frame when a mesh has them (spec: "Materials"; ADR-0048).

- **`MeshData` gains an optional tangent stream** (`vec4`, w the handedness) in its GPU format; a mesh without it is unchanged.
- **The importers read tangents** from glTF and FBX; none are generated when a file has none (generating MikkTSpace is later work).
- **`mesh.vert` passes the tangent** through the world normal matrix with the draw's determinant sign (as `Fenrir/Skinning.glsl` does for skinned tangents); `lit` builds its frame from it, and keeps Schueler's derivative frame when the stream is absent.

**Blocked by:** none

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirAssets/`

**Status:** resolved

- [x] CPU unit tests: the importer keeps a fixture's tangents and handedness; a mesh without them has no stream.
- [x] GL pixel probes: a normal-mapped quad lit the same through the tangent stream and through the derivative frame; a mirrored quad keeps its handedness.

Hand check: DamagedHelmet's normal map reads correctly once ticket 07 brings it in.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

Built 2026-10-31. Decisions to review:

- **Most of it was already there.** `MeshData::tangents`, the glTF and FBX importers' reading of them (FBX's handedness from its binormals), the GPU stream at location 2 and `FenrirSkinTangent` all predate this ticket. What it adds: `mesh.vert` passes the tangent on, `lit` uses it, a mesh without one is no longer warned about, and tests for the import (a new `tangents.gltf` and `tangents.fbx` written by `make_fixtures.py`: a plain quad, one mirrored in U with handedness -1, and one with no tangents; the other fixtures regenerate byte for byte).
- **"No tangents" is the zero tangent.** A mesh without the stream binds the default `(0, 0, 0, 0)` (it was `+X, +1`), and `lit` reads handedness 0 as "use the derivative frame". No `TANGENTS` variant key, so no extra pipelines; the cost is one branch per pixel. Before this, every mesh without tangents drawn by a built-in logged "has no tangents" once, because reflection lists `inTangent` even unread; tangents are now exempt from that warning.
- **The world 3x3, not the normal matrix.** The ticket says "through the world normal matrix"; tangents are surface directions, so `mesh.vert` uses the world 3x3 as `FenrirSkinTangent` does (the two agree under rotation and uniform scale; under non-uniform scale the world 3x3 is right). The handedness is multiplied by the draw's `worldSign`, and `lit` Gram-Schmidts the interpolated tangent against the normal.
- **A fix to the derivative frame.** Offscreen passes run with `GL_UPPER_LEFT` clip control, so `dFdy` points down the screen, and Schueler's frame (which drops its determinant's sign) turned every normal map half a turn: bumps lit as dents. The derivative frame now keeps that sign, which the probe holding the tangent stream to it needed. Normal-mapped scenes without tangents (BoxTextured has no normal map, so no demo scene is known to change) now light the right way up; the tonemapping goldens use the flat normal and did not change.
- **Back faces.** `lit` builds the frame from the front-facing normal and turns the mapped normal over for a back face, as Khronos's sample viewer does, so both frames agree on double-sided materials.
- **Mirrored draws are not front-face flipped.** A draw whose world matrix mirrors is still culled by its screen winding (`CullMode::Back`, counter-clockwise front), so a mirrored single-sided mesh vanishes; glTF says the winding should flip with a negative determinant. The world-mirrored probe winds its quad clockwise to face the camera. Out of scope here; worth its own ticket.
- **No golden image.** The probes compare the two frames exactly and catch a lost handedness (a mutation dropping `worldSign` fails the world-mirrored case); a normal-mapped golden would be redrawn by tickets 04 and 05 anyway.
