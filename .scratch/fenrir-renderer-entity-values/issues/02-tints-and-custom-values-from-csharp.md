# 02: Tints and custom values from C#

Spec: [Per-entity material values](../spec.md).

**What to build:** A C# script reads and writes `colour` and `custom` on `MeshRenderer` and `InstancedMeshRenderer`, through the renderer's scripting extension and the generated bindings.

**Blocked by:** 01
**Touches:** `packages/FenrirRenderer/Scripting/`, `packages/FenrirRenderer/Managed/`

**Status:** ready-for-agent

Beside 03, on 01.

- [ ] FenrirRendererScripting fixture tests: `colour` and `custom` written from C# reach the draw record.
- [ ] The generated bindings are current.
