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

**Status:** ready-for-agent

- [ ] Seam 7: groups placed by order among the test extension's C++ work (a C# system between two C++ items by order, and equal orders with C++ first); an undeclared stage failing the load with the system and stage named; structural changes from a stage system playing back after the hosting work; a throw disabling the system until reload; reload replacing groups; a stage that never runs raising nothing.
- [ ] The existing scripting extensions (input, window, debug draw, assets, renderer, physics) declare no stages and behave as before; all their rules tests pass.
- [ ] All passes under the msvc-x64-debug preset with `FENRIR_ENABLE_SCRIPTING=ON`; `check_tickets.py` and clang-format pass.
