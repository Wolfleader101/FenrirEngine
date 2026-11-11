# 08: The asteroid ring draws full-detail rocks through generated levels, and its panel shows culling

**What to build:** LightingDemo's scene 5 without its hand simplification, showing culling and levels at work (spec: "Problem Statement"; ADR-0057).

- **The vertex clustering goes** (`Simplified` and its cell counts in `examples/LightingDemo/src/AsteroidRing.cpp`): each rock kind loads at full detail, `GenerateLods` makes its levels once, and the ring and the field draw the same mesh.
- **The panel gains** switches for `gpuCulling` and culling freeze, a slider for `lodErrorPixels`, and shows `instances`, `instancesDrawn` and `drawnByLod`, beside the existing draw and instance counts and merging switch.
- **The README's numbers** are measured again in Release at 1280x720 with 4x MSAA, looking across the ring and along it, with culling on and off, and replace the vertex-clustering paragraph; the machine is named.
- The scene's null smoke run still passes without fetched assets.

**Blocked by:** 05, 07

**Touches:** `examples/LightingDemo/`

**Status:** resolved

- [x] The LightingDemo smoke run on null passes, with the panel's new controls driven once each.

Hand check: at full detail with culling and levels on, the ring is at least as fast as the vertex-clustered ring was (about 9 ms on the RTX 5080) and close-up rocks keep their detail; freezing culling and flying out shows only the frozen view's copies and the frustum's edges.

- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

- The rocks' chain stalled at about 400 triangles with every UV-island vertex locked (ticket 06's default), which put the ring at 25 ms, so `LodSettings` gained `lockSeams` (default on, as ticket 06 built it) and the ring turns it off, reaching seven levels and 32 triangles; a FenrirAssets unit test covers it. This touches `packages/FenrirAssets/` beyond the Touches line.
- `Renderer::SetLodSelection` (owner and `App&` static) was added for the live slider; ticket 09 binds it.
- Measured in Release on the RTX 5080 (i7-13700KF) at 1280x720, 4x MSAA, vsync off: across the ring (every copy in view) 12.2 ms on, 96 ms off; along it (22,600 of 100,000 kept) 10.2 ms on, 88 ms off. The 9 ms the hand check cites predates lighting tickets 25 to 31's post effects, so the comparison is the developer's to judge on the same build.

