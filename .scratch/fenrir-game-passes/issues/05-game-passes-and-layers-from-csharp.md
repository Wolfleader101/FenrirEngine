# 05: Game passes and layers from C#

Spec: [Game passes](../spec.md). ADR-0075.

**What to build:** A C# script adds a game pass with `Renderer.AddPass(name, shader, point, inputs)`, enables, disables and removes it and sets its parameters by name through the handle, and reads and writes `layers` on `MeshRenderer` and `InstancedMeshRenderer`, through the renderer's scripting extension and the generated bindings.

**Blocked by:** 02
**Touches:** `packages/FenrirRenderer/Scripting/`, `packages/FenrirRenderer/Managed/`, `packages/FenrirRenderer/api/`

**Status:** ready-for-agent

Branches from 02 and runs beside 03 and 04 (its files are the scripting extension's).

- [ ] FenrirRendererScripting fixture tests: a pass added from C# runs; parameters set from C# reach it; disable and remove work.
- [ ] `layers` written from C# reaches the component.
- [ ] The API description and generated bindings are current.
