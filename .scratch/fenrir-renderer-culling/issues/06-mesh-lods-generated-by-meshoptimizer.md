# 06: Meshes carry levels of detail, and FenrirAssets generates them with meshoptimizer

**What to build:** Levels of detail as mesh data, and a way to make them (spec: "Levels of detail"; ADR-0057, amending ADR-0021).

- **`MeshData::lods`**: a vector of `MeshLod { indices, error }`, each a triangle list into the mesh's own streams and the greatest distance, in the mesh's local units, by which it departs from full detail. Level 0 is the mesh's `indices`, with error 0, and is not in the vector. The store refuses a level with an index out of range, a count that is not a multiple of three, an error that does not grow, or levels on a mesh with more than one submesh; it keeps them through `Replace`.
- **meshoptimizer** (MIT) as a submodule at `packages/FenrirAssets/libs/meshoptimizer`, built as a static library with FenrirAssets as other libs are, its licence recorded where the others are.
- **`GenerateLods(const MeshData&, const LodSettings&) -> MeshData`**: the mesh with its levels made by `meshopt_simplify` with attribute seams locked (normals and UVs, through `meshopt_simplifyWithAttributes` where a mesh has them), each level aiming at `LodSettings::ratio` (0.5) of the previous level's triangles, stopping at `maxLevels` (4), at `minTriangles` (32), or when a level saves less than 10 % more; `error` is meshoptimizer's relative error times `meshopt_simplifyScale`. The full-detail indices are optimised for the vertex cache first, as is each level. A skinned mesh keeps its skin and streams.
- **No import option yet**: the store's `Load` takes no options, so an app calls `GenerateLods` on a loaded mesh and adds or replaces it. The FenrirAssets README says how.
- FenrirAssets' API description gains `MeshLod`, the field and `LodSettings` wherever it describes `MeshData` (generated output only).

**Blocked by:** none

**Touches:** `packages/FenrirAssets/`, `.gitmodules`

**Status:** resolved

- [x] FenrirAssets unit tests: a subdivided sphere's chain has falling triangle counts and rising errors, every index in range, and stops at each limit; a cube (nothing to remove) gets no levels; a skinned mesh keeps its skin and bone bounds; UV seams stay where they were (no vertex moves across one).
- [x] FenrirAssets unit tests: the store refuses each malformed level above with a message naming it, and keeps good levels through `Replace`.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
