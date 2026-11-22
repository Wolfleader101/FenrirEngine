# 03: Fog from C#

Spec: [Plain fog](../spec.md).

**What to build:** A C# script reads and writes the scene's `Fog` as it does `Sky`, through the renderer's scripting extension and the generated bindings, so a game sets and changes fog from C#.

**Blocked by:** 01
**Touches:** `packages/FenrirRenderer/Scripting/`, `packages/FenrirRenderer/Managed/`

**Status:** ready-for-agent

Branches from 01 and runs beside 02 and 04 (its files are the scripting extension's).

- [ ] FenrirRendererScripting fixture test: a `Fog` written from C# is the fog the frame uses.
- [ ] C++'s `Fog` reads back in C#.
- [ ] The generated bindings are current.
