# 08: Systems and the managed runner

**What to build:** `ISystem` (`OnUpdate(in SystemContext)`, with optional `OnCreate`/`OnDestroy`) and `[System(Phase, Order)]` for `PreUpdate`, `Tick`, `Update` and `PostUpdate`. On load, the managed runner discovers and instantiates systems, and native code holds the runner as one Iris `Object`. FenrirScripting registers one *sequential* native system per phase, which calls the runner once, and the runner runs that phase's systems in `Order`, applying deferred changes after each. `SystemContext` carries time, the query entry point, the change buffer, `Log` and (from 09) events. If a system throws, the runner logs the exception with the system's name and disables that system until the next reload. The others keep running.

**Blocked by:** 06

**Status:** ready-for-agent

- [ ] Systems run in phase and `Order`. `Tick` systems run once per fixed step.
- [ ] Scripts are never invoked off the main thread. Assert the thread in debug builds.
- [ ] A throwing system logs once, stops running, and the other systems keep running.
- [ ] `OnCreate` runs after load and `OnDestroy` runs before release or shutdown.
- [ ] One native-to-managed transition per phase per frame (plus queries).

## Comments

Spec: `.scratch/engine-scripting/spec.md` (Loading, scheduling and threading).
