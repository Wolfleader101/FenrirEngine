# 07: The cull pass draws each surviving copy at the coarsest level whose error is under a pixel

**What to build:** Levels of detail on screen: a mesh's levels are drawn by distance, chosen per copy on the GPU (spec: "Levels of detail"; ADR-0057).

- **Upload:** a mesh's index buffer holds level 0 then each of ticket 06's levels back to back; its entry keeps each level's first index, count and error. A mesh without levels has one.
- **Templates:** each run writes one command per level of its mesh, contiguous, each with that level's first index and count and its own base instance; each level's region of the indirection is sized for the whole run, as any copy may land in any level.
- **Selection** in the cull shader, for each survivor: its distance `d` from the camera's position to the sphere's nearest point (zero inside it), and the coarsest level `i` with `error[i] * scale * projection / max(d, near) < RendererConfig::lodErrorPixels`, where `scale` is the copy's largest axis scale and `projection` is the camera's pixels per unit at distance 1 (`height / (2 tan(fov / 2))`, or the orthographic equivalent). The survivor is counted into level `i`'s command. A survivor whose radius projects under `RendererConfig::minProjectedPixels` (0, off) is dropped instead, as if it had failed the frustum.
- **Draws:** each run's `DrawIndexedIndirect` has `drawCount` equal to its mesh's level count, one multi-draw; `DepthPrepass` and `Opaque` share the choice.
- **`RendererConfig::lodErrorPixels`** (1.0) **and `minProjectedPixels`** (0), with `gpuCulling` off forcing level 0 and dropping nothing, for comparison; `RendererStats` gains `drawnByLod`, counts per level from ticket 05's readback. The API description gains them (generated output only).
- A CPU `SelectLod` with the same formula, for tests and the docs.

**Blocked by:** 04, 06

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `packages/FenrirRenderer/api/`

**Status:** resolved

- [x] CPU unit tests: `SelectLod` picks level 0 inside the sphere, coarser levels further out, never past the last, and a scaled copy switches further away than an unscaled one; with `minProjectedPixels` set, a far copy is dropped and the same copy is kept with it 0.
- [x] `RendererProbe` on null: a mesh of three levels uploads one index buffer of all three, writes three templates per run with their first indices, and records multi-draws of three commands in both the prepass and `Opaque`.
- [x] GL readback: copies of a three-level mesh in a line away from the camera land in the levels `SelectLod` gives, exactly.
- [x] GL pixel probes: a hand-made mesh whose level 1 is a different shape (a box whose coarse level is a smaller box) draws its level 1 when far and level 0 when near; with `gpuCulling` off it draws level 0 at both.

Hand check: in the asteroid ring with generated levels, pops between levels are not noticeable at the default `lodErrorPixels`.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
