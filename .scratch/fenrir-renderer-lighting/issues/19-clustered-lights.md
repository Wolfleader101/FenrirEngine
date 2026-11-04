# 19: Lights are clustered per view

**What to build:** Each pixel loops over only the lights that reach its cluster (spec: "The frame"; ADR-0049).

- **A compute pass per view** assigns point, spot and rect lights by `range` to 16×9×24 view-space clusters with exponentially spaced depth slices, into per-view light index lists in set 1.
- **`Fenrir/Lighting.glsl` loops over its pixel's cluster**; the light buffer is unchanged, so materials do not change.
- **A cluster-heat debug view** in LightingDemo.

**Blocked by:** 09

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `examples/LightingDemo/`

**Status:** resolved

- [x] CPU unit tests: cluster bounds and light-sphere tests against brute force.
- [x] GL pixel probes: clustered and unclustered lighting agree for a scene of many lights.
- [x] `RendererProbe` on null: a thousand lights cluster per view.

Hand check: the many-lights scene holds its frame rate at a thousand lights.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

Built 2026-11-02, stacked on ticket 18's branch (#265 to #278 not yet merged). Decisions to review:

- **The pass.** `LightClusters`, a compute pass per view with lights, in the `DepthPrepass` stage, runs
  `builtin:light-clusters` one invocation a cluster (54 groups of 64). The lights are read 64 at a time into shared
  memory, each light's sphere (its range; a rect light's range past its corners) tested against the cluster's
  view-space box, and listed in light buffer order. Slices run exponentially from the near plane (held at least 1e-3)
  to the farthest any light reaches, computed on the CPU per view (`ClusterDepthsOf`), so no depth is wasted past the
  lights.
- **Conservative boxes.** Each box is padded by a hundredth of its size, the edge tiles reach 0.05 NDC past the
  viewport (MSAA can interpolate a sample past it) and the first slice starts at the projection's own near plane (an
  orthographic camera may put it behind itself). The CPU tests check every frustum point is inside the box it is found
  in, and every light reaching a point is listed in its cluster.
- **Lists in set 1, the light buffer untouched.** Each view's cluster buffer (each cluster's count, then room indices;
  a copy per frame in flight) is bound at set 1 binding 3, `Fenrir/View.glsl`'s `fenrirClusterLights`; the view block
  gains `clusterSlices` and `clusterInfo`. `Fenrir/Lighting.glsl` finds its pixel's cluster from the world position
  (`FenrirClusterOf`) and loops over its list, passing the light buffer index on, so shadowed lights work unchanged.
  Every other view block binds a buffer of one 0 and loops over the whole buffer. The light record moved to
  `Fenrir/LightRecord.glsl` so the compute program can declare it without set 0.
- **A cap of 256 lights a cluster** (ADR-0049 amended), the room the light count when fewer. Past it, the lights later
  in the buffer light nothing in that cluster: in Many lights at 1024 lights and range 5 the clusters round the camera
  hit it (the heat view shows them red), so that one configuration darkens slightly against the plain loop.
- **Copies per frame in flight.** One shared buffer per view was planned (only the GPU touches it), but timing showed
  frames stalling on it after clustering was toggled, so each view has a ring of copies, as the skinned vertices do.
- **Not asked for:** `RendererConfig::clusteredLights` and `Renderer::SetClusteredLights` (owner and static App), the
  off switch the GL comparison and the timing need; Many lights' C key and checkbox, its count steps to 1024, and its
  markers in 32 hues so each hue's markers instance (1024 marker materials would have been 1024 draws).
- **Not done:** no depth-bounds culling of empty clusters (every cluster is filled, seen or not); spot lights are
  bounded by their range's sphere, not their cone; no warning when a cluster overflows.
- **Tests:** `Tests/LightClusters.cpp` (CPU: slices, index order, every frustum point inside its cluster's box for
  perspective and orthographic, edge-tile and behind-the-camera points, box faces against an independent unprojection,
  sphere against box by brute force, rect and point spheres, slice depths, every reaching light listed, list order,
  the cap; null: a pass a view before Opaque, an invocation a cluster over 1000 lights with room 256, its depths, a
  buffer a view, Opaque binding it at set 1 binding 3 with the view block saying so, room for fewer lights, no pass
  without lights or with clustering off, back on the next frame), `Tests/OpenGL/LightClusters.cpp` (150 point, spot
  and rect lights, some shadowed, on a wall: clustered and unclustered agree at every pixel to 1e-3; fails with the
  compute pass's radius halved; the heat view colours every lit pixel).
- **msvc-x64-debug:** the whole CTest tree passed (GL included, RTX 5080).

Timing, Many lights in Release on an RTX 5080, 2560 x 1440 window, lights held still, shadows off, each configuration
run twice alternately for 700 frames after 300 of warm-up (the two runs agree within 1%):

| Lights | Range | Clustered | Unclustered |
| --- | --- | --- | --- |
| 1024 | 5 | 8.0 to 8.2 ms | 24.3 to 24.9 ms |
| 1024 | 1.5 | 4.3 ms | 23.9 ms |
| 256 | 5 | 5.0 ms | 6.9 ms |
| 256 | 1.5 | 2.8 ms | 6.1 ms |
| 100 | 5 | 3.4 ms | 4.2 ms |

Screenshots: `screenshots/19-clustered-lights/` (100 lights; 1024 clustered and unclustered; the cluster heat view at
1024). Hand check, by the user: in Many lights at 1024 lights, the frame rate with C on and off, and the heat view as
the range changes.
