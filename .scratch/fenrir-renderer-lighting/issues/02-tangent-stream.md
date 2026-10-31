# 02: Meshes carry an optional tangent stream, and `lit` uses it

**What to build:** The importer keeps glTF's tangents (MikkTSpace) and FBX's when present, `MeshData` holds them as an optional stream, and `builtin:lit` uses them instead of the derivative frame when a mesh has them (spec: "Materials"; ADR-0048).

- **`MeshData` gains an optional tangent stream** (`vec4`, w the handedness) in its GPU format; a mesh without it is unchanged.
- **The importers read tangents** from glTF and FBX; none are generated when a file has none (generating MikkTSpace is later work).
- **`mesh.vert` passes the tangent** through the world normal matrix with the draw's determinant sign (as `Fenrir/Skinning.glsl` does for skinned tangents); `lit` builds its frame from it, and keeps Schueler's derivative frame when the stream is absent.

**Blocked by:** none

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirAssets/`

**Status:** ready-for-agent

- [ ] CPU unit tests: the importer keeps a fixture's tangents and handedness; a mesh without them has no stream.
- [ ] GL pixel probes: a normal-mapped quad lit the same through the tangent stream and through the derivative frame; a mirrored quad keeps its handedness.

Hand check: DamagedHelmet's normal map reads correctly once ticket 07 brings it in.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
