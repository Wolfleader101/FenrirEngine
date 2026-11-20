# 01: Render layers on renderers

Spec: [Game passes](../spec.md). ADR-0087.

**What to build:** `MeshRenderer` and `InstancedMeshRenderer` carry `layers`, a 32-bit bitmask whose default is layer 0's bit, carried into the draw list for later features to read. The API description is regenerated and committed. Nothing reads it yet, so a scene renders exactly as before.

**Blocked by:** None (can start immediately)
**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/Components.hpp`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/api/`, `packages/FenrirRenderer/Tests/`

**Status:** ready-for-agent

Bottom of the stack 01, 02, 03, 04, 06.

- [ ] Both components default to layer 0.
- [ ] The draw list carries each item's layers (a null-backend test).
- [ ] The renderer's tests pass unchanged; the API description is current.
