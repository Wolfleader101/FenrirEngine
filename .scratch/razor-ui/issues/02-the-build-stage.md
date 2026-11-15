# 02: The build stage

**What to build:** C++ and C# work can make and change elements in a hosted stage at the start of the UI's frame, and what they make is styled, laid out and painted that same frame (spec: "FenrirUI: the build stage"; ADR-0068, amending ADR-0039 and ADR-0061).

- `UIRoots::AddBuildStage(order, work)` returns a handle, and `RemoveBuildStage(handle)` removes it, shaped like `AddPaintStage`. The work runs once a frame as the first work of the UI's `PostUpdate`, before any root advances transitions or resolves styles, lowest order first, with equal orders in registration order.
- FenrirUI's scripting extension declares the stage `Fenrir.UI.Build`, and the binding's `[stages]` gives `UIStages.Build`. Structural changes recorded by its C# systems play back when the stage ends, as for `Fenrir.UI.Paint`.
- The glossary's *build stage* and FenrirUI's README say where it runs and what it is for.

**Blocked by:** none

**Touches:** `packages/FenrirUI/include/`, `packages/FenrirUI/src/`, `packages/FenrirUI/Tests/`, `packages/FenrirUI/Scripting/`, `packages/FenrirUI/Managed/Fenrir.UI/Generated/`, `packages/FenrirUI/README.md`

Stacked on 01: the same files.

**Status:** ready-for-agent

- [ ] C++ test: an element created in a build stage's work has a laid-out rectangle and appears in the root's captured draw list on the same frame; two works run in order; a removed work stops running.
- [ ] Scripting test: a `[StageSystem(UIStages.Build)]` creates an element under the root; its rectangle is readable from a paint stage system in the same frame.
- [ ] Without the UI installed, a build stage system never runs and the script still loads.
- [ ] Everything passes under the msvc-x64-debug preset. `check_tickets.py`, clang-format and the binding regeneration check pass.
