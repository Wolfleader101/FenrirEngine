# 12: FenrirScripting extension stages run C# systems inside a package's own stages

**What to build:** a scripting extension declares stages by full name, a C# `ISystem` joins one with `[StageSystem(name, Order = …)]`, and the extension runs that system inside its own work, ordered among its C++ items, so ticket 13's C# pose modifiers run inside `Modify` and not a frame late (spec: "Scripting: the FenrirScripting prerequisite"; ADR-0039). It touches no animation package, so nothing technical gates it; it waits for the C++ animation work to finish because the user chose to build all C# last (2026-10-29).

- **Declaration:** `ScriptingExtension` gains the stages it hosts by full name (`Fenrir.Skeleton.Animate` is ticket 13's; the test extension declares its own).
- **C# side:** `[StageSystem(name, Order = …)]` on an ordinary `ISystem`, registered by the system generator. Naming a stage no loaded extension declares fails the script's load with a diagnostic naming the system and the stage.
- **Placement:** on load, FenrirScripting tells each extension which `(stage, order)` groups the script has; the extension places one C++ item per group in its own work and removes them when the script is released; each item makes one call per frame running its group's systems in name order; equal orders run C++ first.
- **Changes and faults:** structural changes recorded by stage systems play back when the hosting system returns; a throw drops the system's changes and disables it until the next reload; a stage that never runs (package absent, inactive scene) is not an error.
- **Docs:** ADR-0009, ADR-0029 and the README's extension sections amended for stages; GLOSSARY gains "extension stage".

No demo scene: Seam 7 proves it with the test extension, which hosts a stage of its own.

**Blocked by:** 06, 07, 11

**Touches:** `packages/FenrirScripting/`, `packages/Iris/`, `docs/adr/0009-*.md`, `docs/adr/0029-*.md`, `GLOSSARY.md`

**Status:** resolved

- [x] Seam 7: groups placed by order among the test extension's C++ work (a C# system between two C++ items by order, and equal orders with C++ first); an undeclared stage failing the load with the system and stage named; structural changes from a stage system playing back after the hosting work; a throw disabling the system until reload; reload replacing groups; a stage that never runs raising nothing.
- [x] The existing scripting extensions (input, window, debug draw, assets, renderer, physics) declare no stages and behave as before; all their rules tests pass.
- [x] All passes under the msvc-x64-debug preset with `FENRIR_ENABLE_SCRIPTING=ON`; `check_tickets.py` and clang-format pass.

Hand check: read the FenrirScripting README's "Stage systems" (under Systems) and "Hosting stages" (under Scripting extensions) sections and check the placing, ordering, playback and failure rules read as you want ticket 13's pose stages to use them; then `ctest --test-dir <build> -R "^FenrirScripting$" -V` lists the four `[stages]` cases passing (there is no demo scene for this ticket).

## Comments

Built 2026-10-30 on `claude/animation-11-tick-posed` (ticket 11, PR #247, not merged yet), as a stacked PR. Decisions to review:

- **Declaring:** `ScriptingExtension` gained `stages` (full names), `placeStages(App&, std::vector<StageGroup>, StageChanges)` and `removeStages(App&)`. `ExtensionSet` refuses a stage declared twice (by one extension or two), an empty name, and stages without both functions. Extensions that declare none are unchanged: the generated extension makers never set the new members.
- **Groups:** C#'s runner numbers each (stage, order) group and reports every stage system through a new native, `Native.RegisterStageSystem(system, stage, order, group)`, called from the generated host's `ReportStages()` while the systems start (the same slot pattern as the component table). `ScriptSystems::Start` checks every stage against the declared ones before `OnCreate`, so an undeclared stage fails the load with nothing of the script run, then hands each declaring extension its groups sorted by stage and order (an extension with none is still called, with none).
- **`StageGroup` and `StageChanges`** are copyable handles sharing one session per generation. `Stop` calls every `removeStages` before `OnDestroy` and then marks the session dead, so a group an extension forgot runs nothing. `Run()` checks the main thread as a phase does.
- **Equal orders, C++ first** is the hosting extension's job, as the spec puts it: FenrirScripting only hands over the order. ADR-0039 says FenrirSkeleton's registration keeps registration order for ties, so ticket 13 registers groups after its C++ items, or sorts C++ first, as the test extension's bench does.
- **Playback:** stage systems never play back after themselves. The extension calls `StageChanges::PlayBack()` when its hosting work returns. A throwing stage system drops only its own changes: the runner takes `Native.ChangesMark()` before each system and `Native.ChangesDiscardFrom(mark)` on a throw (new `ChangeBuffer::DiscardFrom`).
- **Generator:** `[StageSystem(stage, Order = n)]`; an empty stage is FENRIR0015 and both attributes on one class FENRIR0016, each with a compile-fail case.
- **Seam 7's test extension** hosts `Fenrir.TestExtension.Bench`, a list of C++ items in its extension state that a test runs with `RunBench`; the extension script's `Stages.cs` joins it. `Tests/StageTests.cpp` covers the ticket's cases; the rules tests cover the extension-set rules, `ScriptSystems`' stage rules and `DiscardFrom`.
- **No change to `packages/Iris/`** was needed: strings and ints already cross both ways for natives and instance methods.
