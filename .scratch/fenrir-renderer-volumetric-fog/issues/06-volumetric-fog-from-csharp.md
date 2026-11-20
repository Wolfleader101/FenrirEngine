# 06: Volumetric fog from C#

Spec: [Volumetric fog](../spec.md). ADR-0088.

**What to build:** A C# script turns a camera's volumetric fog on and sets its quality, range and history weight, sets `Fog`'s anisotropy and albedo, and creates and edits `FogVolume` components, through the renderer's scripting extension and the generated bindings.

**Blocked by:** 03, 04, 05
**Touches:** `packages/FenrirRenderer/Scripting/`, `packages/FenrirRenderer/Managed/`

**Status:** ready-for-agent

Branches from 05 and runs beside 07 (its files are the scripting extension's).

- [ ] FenrirRendererScripting fixture tests: the camera's settings, `Fog`'s new fields and a `FogVolume` written from C# reach the frame.
- [ ] The generated bindings are current.
