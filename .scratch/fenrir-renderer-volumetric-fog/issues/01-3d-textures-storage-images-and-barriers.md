# 01: 3D textures, storage images and image barriers in the device

Spec: [Volumetric fog](../spec.md). ADR-0088.

**What to build:** The graphics device gains a 3D texture kind (width, height and depth), a storage usage on textures (read and written by compute shaders as images), and storage-image access in pass descriptors, from which each backend derives the barriers between a pass writing an image and a later pass reading or sampling it, so the renderer still never sees barriers. The null device keeps their books; the OpenGL backend uses image load and store with the memory barriers they need.

**Blocked by:** None (can start immediately)
**Touches:** `packages/FenrirRenderer/src/Device/`, `packages/FenrirRenderer/src/Backend/`, `packages/FenrirRenderer/Tests/`

**Status:** ready-for-agent

Bottom of the stack 01, 02, 03, 04, 05, 07.

- [ ] Null-device tests: 3D and storage textures are created with their size and freed; storage access in a pass descriptor is accepted.
- [ ] A GL resource test (label `gl`, skipped without a GPU): a compute pass writes a 3D storage image and a later pass reads the written values back.
- [ ] The renderer's existing tests pass unchanged.
