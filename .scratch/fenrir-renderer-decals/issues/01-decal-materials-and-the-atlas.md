# 01: Decal materials and the decal atlas

Spec: [Decals](../spec.md).

**What to build:** A decal material is a material type of its own: optional textures and factors for base colour, normal, roughness and metal, and emission, and which of them it writes. The renderer packs decal materials' textures into atlas textures, one per kind, with mip levels and padding so regions do not bleed, repacked when a decal texture is added, changed or released, each material carrying its atlas regions. Nothing draws decals yet.

**Blocked by:** None (can start immediately)
**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/api/`, `packages/FenrirAssets/`

**Status:** ready-for-agent

Bottom of the stack 01, 02, 04, 05.

- [ ] Null-backend tests: decal textures are packed into their kind's atlas with padding; adding, changing and releasing a texture repacks it and updates the regions.
- [ ] The API description is current with the decal material.
