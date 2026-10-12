# 11: DemoApp sample script assembly

**What to build:** A small script project under `examples/DemoApp` (for example a spinning-entity system and a C# component) wired through `.feproj` and `Scripting::Install`, as a smoke test of the whole path in a real windowed app. It is not the test suite. The headless target from 01 remains the proof.

**Blocked by:** 07, 08

**Status:** ready-for-agent

- [ ] With scripting ON, DemoApp visibly runs the sample system on scene entities.
- [ ] With scripting OFF, DemoApp builds and runs unchanged, with no `dotnet`.
- [ ] The README says how to build the script project and run the demo.

## Comments

Spec: `.scratch/engine-scripting/spec.md` (Build).
