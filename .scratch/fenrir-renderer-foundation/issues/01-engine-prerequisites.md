# 01: Engine prerequisites: app resources, a Render phase, the main-thread rule and Colour

**What to build:** The engine gains the neutral capabilities every renderer-stack package rests on, each independently useful (spec: "Engine prerequisites: FenrirApp and FenrirScheduler", "FenrirMath"; ADR-0019, ADR-0025). Nothing renders yet.

- **App-wide resources** (spike lesson 5): `App` gains `GetResource<T>`, `TryGetResource<T>`, `AddResource<T>` and `RemoveResource<T>`, shaped like scene resources: one value per type, destroyed with the App. Documented as the home of package-wide state (input devices, the asset store, window state), reached through the `App` a package owner holds so owners can move.
- **The `Render` phase:** `SchedulePriority` gains `Render`, run once per frame after `PostUpdate` and before the event queues swap. It sees `AfterTicks`' interpolated poses and everything written in `Update` and `PostUpdate`. Writing scene state in `Render` is documented as unsupported.
- **The main-thread rule** (spike lesson 6): sequential systems run on the thread that runs the App, parallel systems on workers. No new scheduler rule; it is documented on `AddSystem` and `AddSequentialSystem`, naming the systems that must be sequential (anything touching SDL or the GPU, and every `Render`-phase system).
- **The `Install` failure shape** (spike lesson 7), written down once where later packages follow it: `X::Install(App&, XConfig)` returns a move-only owner; on failure it logs one diagnostic and returns an owner whose `IsValid()` is `false`, which installs no systems and whose calls do nothing. Physics and Scripting keep their shapes. Each later package ticket implements it for its own owner.
- **`Fenrir::Colour`** in FenrirMath: four linear floats, `Colour::FromSrgb(0xRRGGBBAA)` and named constants (at least the ones debug draw, the fallback material and the sky defaults need: white, black, red, green, blue, yellow, cyan, magenta, grey).

**Blocked by:** None (can start immediately).

**Touches:** `packages/FenrirApp/`, `packages/FenrirScheduler/`, `packages/FenrirMath/include/FenrirMath/`

**Status:** resolved

- [x] FenrirApp tests (through `App::RunFrame`): an app resource is reachable from every phase, replaced by a second `AddResource`, gone after `RemoveResource`, and destroyed with the App (a destructor counter); `TryGetResource` is empty when absent.
- [x] A `Render` system runs once per frame after `PostUpdate` and before the swap: it sees an event sent in `PostUpdate` that frame, and sees each frame's interpolated pose; with zero and with several ticks in a frame it still runs exactly once.
- [x] A sequential system runs on the App's thread (thread id checked), with parallel systems still on workers.
- [x] `Colour::FromSrgb` decodes to linear within a tolerance on known values (0x000000FF, 0xFFFFFFFF, mid grey 0x808080FF), alpha untouched.
- [x] Clang and MSVC x64 debug and release presets build with no new warnings.
