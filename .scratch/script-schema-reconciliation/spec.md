Status: ready-for-agent

# Reconcile script-defined components and events in one module

Follow-up: `.scratch/script-generation-lifecycle/spec.md` is done after this one.

## Problem Statement

When a **script assembly** loads (at install, on hot reload, or when a failed reload is rolled back), the engine must work out what the assembly defines — its **script components** and C#-defined **events** — and compare that with the data the previous **generation** left in the ECS and the App. Today this is written twice, once for components and once for events: discovery, duplicate and size checks, layout fingerprinting, dropping data whose layout changed, and all-or-nothing registration. The registry also keeps twin tables with twin methods. A rule fixed for one kind can silently stay broken for the other, and the whole sequence lives inside one very long function in the same file as install and reload, so a maintainer must read about 250 lines to change one rule.

## Solution

From a maintainer's point of view, one module owns "what the loaded script assembly defines versus what the ECS and App still hold". It runs the same steps for both kinds and differs only in how stale data is cleared (script component storage versus event queue). Behaviour seen by script authors and by the engine does not change: the same all-or-nothing guarantee, the same warnings, the same data kept when a layout is unchanged.

## User Stories

1. As a maintainer, I want one place that decides whether a loaded script assembly's definitions are acceptable, so that I fix a rule once for components and events.
2. As a maintainer, I want the component and event paths to share duplicate-name checking, so that the two cannot drift.
3. As a maintainer, I want the size and alignment checks written once, so that a rejected definition gets the same diagnostic whichever kind it is.
4. As a maintainer, I want layout fingerprinting written once, so that "unchanged layout" means the same thing for components and events.
5. As a maintainer, I want the stale-data policy to be one rule with two small adapters (clear script component storage, remove event queue), so that I can see exactly what differs between the kinds.
6. As a maintainer adding a third kind of script-defined thing, I want to supply only an adapter, so that I do not copy the pipeline a third time.
7. As a script author, I want a script assembly with a duplicate component name to be refused with a message naming it, so that I can fix it.
8. As a script author, I want a script assembly with a duplicate event name to be refused with a message naming it, so that I can fix it.
9. As a script author, I want a component that reports a zero or invalid size to be refused with a clear message, so that I do not get silent corruption.
10. As a script author, I want an event that reports an invalid size to be refused the same way.
11. As a script author, I want a script component that reuses the name of a native component to be refused, so that I cannot shadow engine data.
12. As a script author, I want a script event that collides with a queue the App already owns (and the previous generation does not own) to be refused.
13. As a script author, I want component data kept across a hot reload when I did not change the component's layout, so that entity state survives edits to systems.
14. As a script author, I want event queues kept, with their queued events, when the event's layout is unchanged.
15. As a script author, I want component data removed, with a warning that says how many were removed, when I change a component's layout, so that no system reads old bytes as the new layout.
16. As a script author, I want the same for a component I deleted.
17. As a script author, I want an event queue dropped, with a warning, when I change or delete the event.
18. As a script author, I want a failed load to leave the registry, the ECS and the App exactly as they were, so that a bad build never half-applies.
19. As a script author, I want a native component or event whose C# struct no longer matches to fail the whole load, so that I find out at once.
20. As an engine integrator, I want native components and events I registered in the config to be checked against the script assembly in the same pass, so that mismatches are reported together with script-defined problems.
21. As an engine integrator, I want registration of script definitions to be all-or-nothing, so that I never see a registry with some of a script's components.
22. As an engine integrator, I want the registry's generation to change whenever the table changes, so that C# re-resolves cached ids.
23. As a maintainer, I want the registry to expose one way to add a batch atomically, so that the "add all or none" rule is not duplicated per kind.
24. As a maintainer, I want the discovery calls from managed code to be driven through one interface, so that the active-context "discovering" slots are handled in one place.
25. As a maintainer, I want the module testable without private access, so that its behaviour is asserted through observable outcomes.
26. As a maintainer reading the load path, I want it to read "reconcile, then keep", so that the order of checks is visible and not buried in a long function.
27. As a maintainer, I want the log wording for dropped data to stay stable, so that existing tests and user expectations keep working.
28. As a user running a game, I want none of this to change frame behaviour, so that the refactor is invisible.

## Implementation Decisions

- Introduce a single reconciliation module in the scripting package. Its interface takes the engine context, the component registry, the freshly loaded Iris component and the record of layouts already owned by the ECS and App, and either commits the whole load or changes nothing.
- The module runs, in order: discover what the assembly defines, check it, validate every native component and event against its C# struct, compare layouts with what is owned, drop stale data, register all-or-nothing, create missing event queues, and record ownership. This is the order the current code uses; it is preserved.
- Two adapters at the seam, for the two kinds that actually exist: script components (clearing means removing that dynamic component type from every scene) and events (clearing means removing the event queue from the App). A third kind is out of scope but must need only a new adapter.
- The ECS stays behind FenrirECS (ADR-0008): the module uses only `DynamicComponentType`, `ClearDynamic` and the existing App event queue calls, and never names the ECS library.
- The component registry exposes one atomic "add these registrations" operation per table, with the duplicate-in-batch and already-taken checks shared. Ids remain positions; the generation still changes on every mutation.
- Messages keep their current text (tests and users depend on it). Diagnostics from a refused load still begin by saying the script assembly was released.
- The public `Scripting` interface, `ScriptingConfig`, registration types and the C# contract do not change. No ADR is affected; no new glossary term is needed, since the terms *script component*, *event*, *script assembly* and *generation* already cover it.
- Done when the load path in the main scripting source no longer contains the discovery/check/diff logic for components or events, and no behaviour differs.

## Testing Decisions

- A good test here exercises external behaviour only: what the engine and logger show after Install or Reload, and what is left in the ECS and App. It does not assert on internal function calls or on the structure of the new module.
- The one seam is the existing one: `Scripting::Install`, `Scripting::Reload` and `ApplyChanges` over the headless app, using the existing fixture assemblies (including the second-version fixture used for reload tests).
- Cases to cover through that seam, written once per behaviour and, where the behaviour applies to both kinds, once for components and once for events: duplicate name refused; invalid size refused; collision with a native name refused; unchanged layout keeps data; changed or removed definition drops data with a warning; a refused load leaves registry, ECS and App unchanged; a native struct mismatch fails the whole load; a successful load registers everything together.
- Prior art: the existing reload, all-or-nothing and event tests in the scripting test file. Existing tests must pass unchanged; new tests are added only for behaviour not already covered.

## Out of Scope

- Changing hot-reload timing, copying or unloading (see the generation lifecycle spec).
- Changing the C# contract, generators, or the Iris API.
- Adding a new kind of script-defined thing.
- A FenrirECS test target.
- Splitting the large test file.

## Further Notes

- Origin: architecture review of FenrirScripting, candidate 2. The review judged the seam real because exactly two adapters (components, events) already exist.
- Do this before the generation lifecycle spec: it removes most of the load-completion logic that the lifecycle extraction would otherwise have to move.
