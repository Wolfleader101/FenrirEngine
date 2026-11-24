# 02: The app and time from C#

Spec: [C# reach: parenting, scenes, the app, C# resources and mesh positions](../spec.md).

**What to build:** A C# script stops the app with `App.Stop()`, which ends the run after the current frame as `App::Stop` does, and reads `Time.Elapsed` (seconds since the app started, as a double), `Time.FrameCount` and `Time.TickCount` beside `Time.DeltaTime` and `Time.TickRate`, all read-only. Made through FenrirScripting's own bindings, the API description and the generated pipeline.

**Blocked by:** None (can start immediately)
**Touches:** `packages/FenrirScripting/src/`, `packages/FenrirScripting/api/`, `packages/FenrirScripting/Managed/Fenrir.Scripting/`, `packages/FenrirScripting/Tests/`

**Status:** resolved

Bottom of the stack 02, 03, 05, 06 (they share FenrirScripting's bindings, API description and generated code).

- [x] A FenrirScripting test calls `App.Stop()` from the script fixture and checks the headless app stops after that frame.
- [x] A test reads elapsed time and the frame and tick counts from C# and checks them against the app's own after some frames and ticks.
- [x] The API description and generated bindings are current.
