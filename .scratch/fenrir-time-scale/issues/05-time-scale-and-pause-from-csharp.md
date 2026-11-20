# 05: Time scale and pause from C#

Spec: [Pause and time scale](../spec.md). ADR-0086.

**What to build:** A C# script reads and writes `Time.Scale` and `Time.Paused` and reads `Time.UnscaledDeltaTime`, beside `Time.DeltaTime` (now scaled) and `Time.TickRate`, through FenrirScripting's own bindings, the API description and the generated pipeline.

**Blocked by:** 01
**Touches:** `packages/FenrirScripting/src/`, `packages/FenrirScripting/api/`, `packages/FenrirScripting/Managed/Fenrir.Scripting/`, `packages/FenrirScripting/Tests/`

**Status:** ready-for-agent

Beside 02, 03 and 04, on 01. It touches the same time bindings as [The app and time from C#](../../fenrir-scripting-reach/issues/02-the-app-and-time-from-csharp.md) in the C# reach spec: build it after that ticket, stacked on its branch.

- [ ] Fixture tests: `Time.Scale` and `Time.Paused` set from C# reach the app, and resuming restores the scale.
- [ ] `Time.DeltaTime` reads the scaled delta and `Time.UnscaledDeltaTime` the unscaled one.
- [ ] The API description and generated bindings are current.
