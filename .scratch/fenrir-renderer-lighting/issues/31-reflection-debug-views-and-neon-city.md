# 31: Reflection debug views, LightingDemo's controls, and the neon city's wet streets

**What to build:** Screen-space reflections (ADR-0056) made visible and adjustable, and turned on where they were asked for. Requested by the user while reviewing ticket 20 (the neon city), 2026-11-02, comparing it with cyberpunk reference renders.

- **`CameraDebugView` gains `Reflections`** (the reflected colour times its confidence, exposed and tonemapped, black where nothing reflects) and **`ReflectionConfidence`** (grey).
- **LightingDemo's Lighting panel** gains a Reflections section: on/off, intensity, max distance, thickness and roughness cutoff, and the two debug views.
- **The neon city** turns reflections on: its wet streets reflect the towers' glowing edge strips, the wall boards and the lit windows, as well as the signs' rect lights. Its signs' boards are drawn, so their rect lights keep `visibleSurface` true. A key toggles reflections, beside M's wet and dry streets. Its golden image is updated.
- **The showroom** turns them on for its polished floor.
- **C#:** the two debug views reach C# through FenrirRenderer's regenerated binding, and join the scripting round trip.

**Blocked by:** 20, 30

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/Scripting/`, `examples/LightingDemo/`

**Status:** ready-for-agent

- [ ] GL pixel probes: each debug view shows the probed pixel's reflection or confidence; with reflections off both are black.
- [ ] The neon city's and the showroom's smoke runs pass on null with reflections on, without fetched assets.
- [ ] The three passes' GPU time in the neon city at 1920 × 1080, measured with RenderDoc or Nsight, is recorded in the ticket's comments, and over ADR-0056's 1.5 ms is called out.

Hand check: in the neon city at night, the wet streets mirror the edge strips, sign boards and windows, streaked by the puddles' edges and blurred on damp asphalt; reflections fade at the screen's edge and fall back to the signs' own glints as they leave the screen; driving cars leave no visible ghost at walking speed.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
