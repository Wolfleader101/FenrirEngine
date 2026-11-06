# 08: The asteroid ring draws full-detail rocks through generated levels, and its panel shows culling

**What to build:** LightingDemo's scene 5 without its hand simplification, showing culling and levels at work (spec: "Problem Statement"; ADR-0057).

- **The vertex clustering goes** (`Simplified` and its cell counts in `examples/LightingDemo/src/AsteroidRing.cpp`): each rock kind loads at full detail, `GenerateLods` makes its levels once, and the ring and the field draw the same mesh.
- **The panel gains** switches for `gpuCulling` and culling freeze, a slider for `lodErrorPixels`, and shows `instances`, `instancesDrawn` and `drawnByLod`, beside the existing draw and instance counts and merging switch.
- **The README's numbers** are measured again in Release at 1280x720 with 4x MSAA, looking across the ring and along it, with culling on and off, and replace the vertex-clustering paragraph; the machine is named.
- The scene's null smoke run still passes without fetched assets.

**Blocked by:** 05, 07

**Touches:** `examples/LightingDemo/`

**Status:** ready-for-agent

- [ ] The LightingDemo smoke run on null passes, with the panel's new controls driven once each.

Hand check: at full detail with culling and levels on, the ring is at least as fast as the vertex-clustered ring was (about 9 ms on the RTX 5080) and close-up rocks keep their detail; freezing culling and flying out shows only the frozen view's copies and the frustum's edges.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
