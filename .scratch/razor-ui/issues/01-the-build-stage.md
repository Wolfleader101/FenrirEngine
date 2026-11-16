# 01: The build stage

**What to build:** C++ work and a C# `[StageSystem(UIStages.Build)]` make and change elements at the start of the UI's `PostUpdate`, and what they make is styled, laid out and painted the same frame. This is the prefactor the Razor dispatcher runs in (spec: "FenrirUI: the build stage"; ADR-0068, amending ADR-0039 and ADR-0061).

- `UIRoots::AddBuildStage(order, work)` returns a handle that `RemoveBuildStage` takes, shaped like `AddPaintStage`. The work runs once a frame as the first work of the UI's `PostUpdate`, before any root advances transitions or resolves styles: lowest order first, equal orders in registration order.
- FenrirUI's scripting extension declares `Fenrir.UI.Build`, and the binding's `[stages]` gives `UIStages.Build`. Structural changes recorded by its C# systems play back when the stage ends, as for `Fenrir.UI.Paint`.
- FenrirUI's README says where the stage runs and what it is for.

**Blocked by:** none

**Touches:** `packages/FenrirUI/include/FenrirUI/UI.hpp`, `packages/FenrirUI/src/`, `packages/FenrirUI/Tests/`, `packages/FenrirUI/Scripting/`, `packages/FenrirUI/Managed/Fenrir.UI/Generated/`, `packages/FenrirUI/README.md`

**Status:** resolved

- [x] C++ test: an element created in a build stage's work has a laid-out rectangle and appears in the root's captured draw list on the same frame. Two works run in order. A removed work stops running.
- [x] Scripting test: a `[StageSystem(UIStages.Build)]` creates an element under a root, and a paint stage system reads its rectangle in the same frame.
- [x] Without the UI installed, a build stage system never runs and the script still loads.
- [x] Everything passes under the msvc-x64-debug preset. `check_tickets.py`, clang-format and the binding regeneration check pass.
