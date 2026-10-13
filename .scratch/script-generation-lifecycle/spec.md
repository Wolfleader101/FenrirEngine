Status: ready-for-agent

# Separate the reload watcher and generation lifecycle from the scripting host state

Depends on: `.scratch/script-schema-reconciliation/spec.md` (do that first).

## Problem Statement

One internal state object in the scripting package carries about thirty fields and eight concerns: the logger, the Iris host and loaded component, the systems runner, the file watcher (stamp, debounce, poll time), the private copies of the script assembly on disk, the list of released **generations** waiting to be collected, the owned layouts, and the reload-in-progress flag. Install, polling, copy management, loading, rollback and retirement all reach into it. A maintainer changing reload behaviour has to hold all of it in their head, and the watcher's rules (the file must stop changing, stay unchanged for the debounce time, and be openable) can only be exercised by starting .NET and rewriting a real file.

## Solution

From a maintainer's point of view, the generation lifecycle (making a private copy, loading it all-or-nothing, replacing the running generation with rollback if the new one fails, retiring and sweeping released generations) is one module, and the decision "is the script assembly file ready to reload?" is another piece of that module's behaviour rather than something interleaved with Iris calls. Behaviour for script authors and the engine is unchanged: same reload order, same rollback, same logs, same configuration.

## User Stories

1. As a maintainer, I want the generation lifecycle in one module, so that reload bugs have one home.
2. As a maintainer, I want the file-stability decision (changed, settling, ready) separate from the act of reloading, so that I can reason about each alone.
3. As a maintainer, I want the scripting host state to hold only what the host itself owns (logger, Iris host, phase runner), so that its size matches its job.
4. As a maintainer, I want copy directories created and deleted by the module that loads them, so that disk cleanup has one owner.
5. As a maintainer, I want retired generations tracked in the same module that retires them, so that the pending-unload count has one source.
6. As a script author, I want a rebuilt script assembly to be picked up between frames once the build has finished, so that I never load a half-written file.
7. As a script author, I want a file that keeps changing to delay the reload until it is stable.
8. As a script author, I want a file that is locked or unreadable to delay the reload, not fail it.
9. As a script author, I want a build that cannot overwrite the original to be impossible, because the loaded assembly is always a private copy.
10. As a script author, I want a new build that fails to load to be reported and the previous build to run again, so that a typo does not stop my session.
11. As a script author, I want a message when even the previous build cannot be restored, saying no scripts are running.
12. As a script author, I want the old systems told to shut down before their generation is released, so that they can clean up.
13. As a script author, I want a generation that does not unload in time to be warned about once, naming what likely holds it.
14. As a script author, I want the old generation's disk copy removed once it is collected, and kept while it is not.
15. As an engine integrator, I want `Reload()` to behave exactly as before, in the same order, including when called with nothing to reload.
16. As an engine integrator, I want `Generation()` and `PendingUnloadCount()` to report the same values as before.
17. As an engine integrator, I want hot reload disabled by configuration to mean the watcher never acts but `Reload()` still works.
18. As an engine integrator, I want recorded structural changes played back before a reload so that no entity is left half made.
19. As an engine integrator, I want a reload that throws to be logged and leave scripting usable, not to crash the frame.
20. As an engine integrator, I want shutdown to release the loaded generation and retired ones without waiting forever.
21. As a maintainer, I want the reload-in-progress guard to live with the code it protects, so that re-entrancy is handled in one place.
22. As a maintainer, I want debounce and polling intervals to be inputs, not hidden clocks, so that behaviour is repeatable.
23. As a maintainer, I want the module's failures returned as results, not thrown across its interface.
24. As a user running a game, I want no change in frame cost when nothing is being reloaded.

## Implementation Decisions

- Extract a generation lifecycle module that owns: the scratch directory and per-generation copies (active, previous kept for restore, retired), the loaded Iris component, the generation counter, the retired list and its sweep, and the replace-with-rollback sequence. Its collaborators are supplied to it, not created inside it: the Iris host, a hook that completes a load (the reconciliation module from the previous spec) and a hook that starts or stops the systems for a generation.
- Within it, the watcher's decision ("nothing to do", "still settling", "ready to reload") is computed from the current time and the file's stamp, and the check that the file can be opened. It takes time as an input.
- The reload order is preserved exactly: play back recorded changes, stop the old systems, release and unload the old generation (waiting for it up to the configured timeout), load the new one all-or-nothing, and if that fails load the previous copy again.
- The remaining host state keeps the logger and its mutex (Iris messages arrive on any thread), the Iris host, the phase runner and the process-wide engine context activation. Install stays the owner of startup ordering.
- The public `Scripting` interface and `ScriptingConfig` do not change. Config values (intervals, debounce, unload timeout, reload directory, hot reload flag) are passed to the new module at construction.
- The glossary already defines *generation* (in the Iris section); add a short engine-level term to `GLOSSARY.md` only if the module's name introduces a concept not already there. No ADR is affected. The hand-off from retired generations to collection follows ADR-0005.
- Done when the host state no longer holds file-stamp, debounce, copy-directory or retired-list fields, and the existing reload behaviour is unchanged.

## Testing Decisions

- A good test asserts what script authors and the engine can observe: which build is running, what the logger says, what remains on disk, `Generation()`, `PendingUnloadCount()`. It does not inspect the new module's internals.
- The one seam is the existing one: `Scripting::Install` and `Scripting::Reload` over the headless app with the fixture assemblies and the second-version fixture. Watcher timing is driven by the configuration (interval and debounce set to zero or small values), as the current reload tests do; no new test seam is added.
- Cases to cover or keep: a rebuilt file is picked up between frames; a file that keeps changing is not loaded until stable; a bad new build rolls back to the previous one; a bad new build with a bad previous one leaves no scripts and says so; the old copy is deleted once collected and kept while pending; hot reload off means no automatic reload but `Reload()` works; a reload with nothing loaded fails cleanly.
- Prior art: the existing hot-reload, rollback and unload tests in the scripting test file. They must pass unchanged.

## Out of Scope

- A pure, clock-injected watcher test seam (the review proposed it; this spec chose the existing seam only).
- Reconciliation of script components and events (previous spec).
- Narrowing the public `Scripting` interface or moving diagnostics out of it.
- Restructuring `Install` into stages.
- Changing reload semantics, defaults, or the Iris API.

## Further Notes

- Origin: architecture review of FenrirScripting, candidate 1.
- If watcher timing proves flaky to test at the existing seam, revisit the second seam as a follow-up rather than widening this spec.
