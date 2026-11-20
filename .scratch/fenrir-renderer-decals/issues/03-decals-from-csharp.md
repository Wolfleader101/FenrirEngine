# 03: Decals from C#

Spec: [Decals](../spec.md).

**What to build:** A C# script creates and edits `Decal` components and decal materials, through the renderer's scripting extension and the generated bindings.

**Blocked by:** 02
**Touches:** `packages/FenrirRenderer/Scripting/`, `packages/FenrirRenderer/Managed/`

**Status:** ready-for-agent

Branches from 02 and runs beside 04 and 05 (its files are the scripting extension's).

- [ ] FenrirRendererScripting fixture tests: a `Decal` and a decal material written from C# reach the frame.
- [ ] The generated bindings are current.
