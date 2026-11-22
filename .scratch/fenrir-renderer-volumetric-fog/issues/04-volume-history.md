# 04: The volume's history

Spec: [Volumetric fog](../spec.md). ADR-0088.

**What to build:** The volume is smoothed over frames: a fixed per-frame jitter sequence inside each cell, and a blend with the camera's history volume reprojected by the previous frame's view and projection, at the camera's history weight (default 0.9). History is reset when the volume is remade.

**Blocked by:** 02
**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/api/`

**Status:** ready-for-agent

Stacked on 03.

- [ ] Null-backend tests: the history volume is kept per camera across frames and reset when quality or view size changes.
- [ ] The jitter sequence is fixed, so a run of N frames is repeatable.
- [ ] The history weight from the camera reaches the blend.
