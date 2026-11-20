# 02: Fullscreen from C#

Spec: [Borderless fullscreen](../spec.md).

**What to build:** A C# script reads and writes `Window.Fullscreen` (true is borderless fullscreen) and the window mode itself, through FenrirWindow's scripting extension and the generated bindings.

**Blocked by:** 01
**Touches:** `packages/FenrirWindow/Scripting/`, `packages/FenrirWindow/Managed/`

**Status:** ready-for-agent

Beside 03, on 01.

- [ ] FenrirWindowScripting fixture tests: `Window.Fullscreen` written from C# changes the mode and reads back.
- [ ] The generated bindings are current.
