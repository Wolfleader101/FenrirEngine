# 08: Systems and the managed runner

**What to build:** `ISystem` (`OnUpdate(in SystemContext)`, with optional `OnCreate`/`OnDestroy`) and `[System(Phase, Order)]` for `PreUpdate`, `Tick`, `Update` and `PostUpdate`. On load, the managed runner discovers and instantiates systems, and native code holds the runner as one Iris `Object`. FenrirScripting registers one *sequential* native system per phase, which calls the runner once, and the runner runs that phase's systems in `Order`, applying deferred changes after each. `SystemContext` carries time, the query entry point, the change buffer, `Log` and (from 09) events. If a system throws, the runner logs the exception with the system's name and disables that system until the next reload. The others keep running.

**Blocked by:** 06

**Status:** resolved

- [x] Systems run in phase and `Order`. `Tick` systems run once per fixed step.
- [~] Scripts are never invoked off the main thread. Assert the thread in debug builds.
- [x] A throwing system logs once, stops running, and the other systems keep running.
- [x] `OnCreate` runs after load and `OnDestroy` runs before release or shutdown.
- [x] One native-to-managed transition per phase per frame (plus queries).

## Comments

### Resolution

- Contract: `Phase`, `[System]`, `ISystem` (default `OnCreate`/`OnDestroy`), `SystemContext` (`Time`, `Log`, `CreateEntity()`, and `Query<...>()` emitted by the query generator) and `SystemRunner`.
- The SDK generator (`SystemGenerator`) writes `Fenrir.Scripting.Generated.SystemHost` for the script assembly: it holds a `SystemRunner` built from the discovered systems and exposes `PhaseMask`, `OnCreate`, `Run(phase)` and `OnDestroy`. Native code holds it as one Iris `Object`. Diagnostics FENRIR0010 to 0013; compile tests cover 0010, 0011 and 0013 (0012 is a warning and has no test).
- `Scripting` registers one sequential native system per phase that has systems. The runner applies the change buffer after each system (new internal calls `ChangesApply`/`ChangesDiscard`), which is the playback point ticket 06 left open. A throw drops that call's changes and disables the system.
- `OnCreate` runs inside `Install` after load; `OnDestroy` runs in `Shutdown` before the runner object and the component are released. A failure to make or start the runner releases the script assembly like any failed load.
- Tests: phase and `Order`, changes visible to the next system, a throwing system logged once while the others run, `Tick` once per fixed step (compared with a native Tick system), exactly one entry into the script per phase per frame (`PhaseCallCount()`), `OnCreate`/`OnDestroy` timing, nothing runs after `Shutdown`.
- `SystemContext` has no separate "change buffer" member: structural changes go through `Entity.Create/Destroy/Add/Remove` (and `ctx.CreateEntity()`), which use the one buffer the runner plays back after each system.
- A system whose constructor throws is logged and disabled, but the load still succeeds with the others: that is per-system isolation, not the all-or-nothing load the spec describes for components. Only one generation exists until ticket 10, which owns "the previous generation keeps running".
- If starting the runner fails after it was made, `Shutdown` runs `OnDestroy` on systems whose `OnCreate` may not have run. Acceptable for now; ticket 10 revisits load failure.
- **Not tested: the main-thread assert.** It is an `assert` in debug and a logged skip in release, but a debug assert aborts the process so a test cannot trigger it. Verified by reading only.
- The test fixture's systems do nothing until `SystemProbe.Enable()` is called, so tests that load the fixture for other reasons are unaffected.
- A system's `OnCreate` runs before the first frame and so before the engine has entities from a scene load; whether scene loading should re-run it is a hot-reload/scene question for ticket 10.

Spec: `.scratch/engine-scripting/spec.md` (Loading, scheduling and threading).
