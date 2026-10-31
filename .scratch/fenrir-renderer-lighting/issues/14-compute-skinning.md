# 14: Skinned meshes are skinned once a frame in compute

**What to build:** Every later pass draws skinned meshes from per-frame skinned vertex buffers (spec: "Shadows"; ADR-0050, amending ADR-0035).

- **A compute pass** at the start of the frame skins each drawn skinned mesh once into a per-frame vertex buffer (position, normal, tangent), ring-buffered per frame in flight, from ADR-0035's palette buffer.
- **Every pass draws those buffers rigidly**; deduplication, skip cases and bounds are unchanged.
- **`SKINNED` stays** for user shaders without a compute path and as the fallback when `RendererConfig` turns compute skinning off.

**Blocked by:** 10

**Touches:** `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`

**Status:** ready-for-agent

- [ ] GL pixel probes: compute-skinned and vertex-skinned results match for ADR-0035's test meshes.
- [ ] `RendererProbe` on null: one skinning dispatch per skinned mesh per frame however many passes draw it.

Hand check: AnimationDemo looks the same with compute skinning on and off.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
