# 02: Dead zones from C#

Spec: [Stick and trigger dead zones](../spec.md).

**What to build:** A C# script sets a gamepad's dead zones (each stick's inner and outer values and the triggers' threshold and outer value, for every pad or one pad) and reads dead-zoned and raw axis values, through FenrirInput's scripting extension and the generated bindings.

**Blocked by:** 01
**Touches:** `packages/FenrirInput/Scripting/`, `packages/FenrirInput/Managed/`

**Status:** ready-for-agent

Beside 03, on 01.

- [ ] FenrirInputScripting fixture tests: settings written from C# reach the pad; dead-zoned and raw values read in C#.
- [ ] The generated bindings are current.
