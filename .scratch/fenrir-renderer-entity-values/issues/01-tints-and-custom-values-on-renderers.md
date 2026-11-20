# 01: Tints and custom values on renderers

Spec: [Per-entity material values](../spec.md).

**What to build:** `MeshRenderer` and `InstancedMeshRenderer` carry `colour` (linear, default white) and `custom` (four vec4 values, default zero), described for the API description. Every draw record written from them carries the entity's tint in its existing colour and the custom values in four new vec4s (the record grows from 192 to 256 bytes in std430, its C++ mirror and static checks following); an instance list's copies multiply their own colour by the entity's tint, and the custom values apply to every copy. The shared draw include gains an accessor for the current draw's custom values; the built-in lit and unlit programs already multiply by the colour. Automatic instancing and culling are unchanged, so entities with different values still merge into one instanced draw.

**Blocked by:** None (can start immediately)
**Touches:** `packages/FenrirRenderer/include/FenrirRenderer/Components.hpp`, `packages/FenrirRenderer/src/`, `packages/FenrirRenderer/Shaders/`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/api/`

**Status:** ready-for-agent

First; 02 and 03 build on it side by side.

- [ ] Null-backend instancing tests: entities sharing a mesh and material with different tints and custom values still merge into one instanced draw in DepthPrepass and Opaque.
- [ ] Each record carries its entity's values; an instance list copy's colour is multiplied by its entity's tint; defaults leave existing records unchanged.
- [ ] A GL draw test (label `gl`): three copies of one mesh in different tints, and a test shader writing a custom value as colour, checked by reading back pixels.
- [ ] The API description is current.
