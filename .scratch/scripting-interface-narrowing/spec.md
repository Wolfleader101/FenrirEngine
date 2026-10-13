Status: resolved

# Narrow Scripting's public interface to features, behind one liveness check

Origin: architecture review of FenrirScripting, candidate 3.

## Problem Statement

`Scripting`, the module the engine calls to run C# scripts, has a public interface that mixes what an engine integrator needs (install, shutdown, register native components and events, play back or discard structural changes, reload, find a component) with things that exist only so tests can look inside: three diagnostic counters (how many times the engine called into systems, how many query calls scripts made, how many changes are pending) and a raw pointer to the loaded Iris component. Every engine caller has to learn these, and the raw Iris component handle lets any caller reach past the module into the .NET hosting library. Separately, "is scripting actually live (state exists, the engine context is active, an App is attached)?" is re-derived by hand in several methods, so a change to what live means has to be made in each.

## Solution

From an engine integrator's point of view, `Scripting` offers only features. The diagnostics and the Iris component handle are available to the scripting package's own tests through a probe that tests include, and are no longer part of the public header. Inside `Scripting`, the liveness rule is written once and the methods that need it ask for it.

## User Stories

1. As an engine integrator, I want `Scripting`'s public header to list only features, so that I learn only what I use.
2. As an engine integrator, I want no raw Iris component handle on `Scripting`, so that I cannot bypass the module and call into .NET directly.
3. As an engine integrator, I want `ApplyChanges`, `DiscardChanges`, `Reload`, `Generation`, `PendingUnloadCount`, `HasScripts`, `IsRunning`, `FindComponent` and the register calls to keep working exactly as before, so that nothing I wrote changes.
4. As an engine integrator, I want `DiscardChanges` to stay public, so that code that catches a failing script can drop its recorded changes (the README documents it).
5. As a test author, I want one probe that exposes the diagnostic counters, so that tests that check "one transition per phase" and "a fixed number of query calls" keep working.
6. As a test author, I want the probe to give me the loaded script assembly so that I can call fixture methods by name, as the tests do today.
7. As a test author, I want the probe to live with the test harness, so that production code never depends on it.
8. As a maintainer, I want "scripting is live" decided in one place, so that I change its meaning once.
9. As a maintainer, I want every public method that needs a live scripting to behave the same when it is not live (no-op, empty result, or the existing failure status), so that the behaviour on a missing or shut-down `Scripting` is uniform.
10. As a maintainer, I want the behaviour of a moved-from or never-started `Scripting` to be pinned by a test, so that the refactor cannot change it silently.
11. As a maintainer, I want the diagnostics to remain cheap, so that the engine's hot paths pay nothing for them.
12. As a maintainer, I want the README and header comments to stop describing the removed members as public, so that the documentation matches the interface.
13. As a script author, I want no change to how scripts behave, so that this refactor is invisible to me.
14. As a user running a game, I want no change in frame cost.

## Implementation Decisions

- Remove from the public interface of `Scripting`: the phase-call counter, the query-call counter, the pending-change counter and the accessor for the loaded Iris component. They move behind a probe: a small type, defined in the scripting package's test support, that is a declared friend of `Scripting` and reads the same state. Nothing in production code includes it.
- Keep public: install, `IsRunning`, `HasScripts`, `Shutdown`, the register calls and their templates, `FindComponent`, `ApplyChanges` and its summary type, `DiscardChanges`, `Reload`, `Generation`, `PendingUnloadCount`. Their meaning is unchanged.
- The counters themselves stay where they are (they are part of the engine context and the systems runner); only the way to read them changes.
- Introduce one private rule for "live" (state present, engine context active, an App attached) and use it from every method that today repeats the check. Methods that today return a status when not live keep returning that status; methods that no-op keep no-opping.
- The Iris component handle the probe returns is the same pointer the module uses internally; the probe adds no new ownership.
- Update the scripting README and the header's doc comments to match. No ADR is affected; no new glossary term.
- Done when the public header no longer mentions the removed members, the tests compile against the probe, and no behaviour differs.

## Testing Decisions

- A good test crosses the same seam as before: install, run frames, observe. Nothing new is tested about the removed members except that the probe reports what the public ones used to.
- The seam is the existing one, `Scripting::Install` and friends over the headless app and the fixture assemblies, plus the probe as the one test-only addition to that seam. No other seam is added.
- Existing tests that call the counters or the component accessor are changed to go through the probe, and otherwise pass unchanged.
- Add one test that a never-started `Scripting` (empty config), a failed install, a moved-from `Scripting` and a shut-down `Scripting` all answer the public methods the same way they do today: no crash, the existing status or empty result.
- Prior art: the existing install-failure, shutdown and second-install tests in the scripting test file.

## Out of Scope

- Changing what any public method does.
- Moving or removing `PendingUnloadCount` and `Generation`.
- The reconciliation and generation-lifecycle refactors (already done) and the FenrirECS test target (separate spec).
- A general test-support library.

## Further Notes

- The header comment on the component accessor calls it "the hook the runner and the tests call into scripts through". The runner is now in C# and uses its own object, so only tests need it; confirm no other caller exists when implementing.
- The probe makes the tests' reach past the interface explicit and keeps it out of the product interface.
