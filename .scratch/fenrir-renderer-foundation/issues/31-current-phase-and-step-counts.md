# 31: The current phase and the frame and tick counts

**What to build:** FenrirApp tells any system which phase is running and how many frames and fixed steps have begun, so a package can follow the reading phase without systems of its own at phase edges (spec: "Engine prerequisites: FenrirApp and FenrirScheduler"; ADR-0017's phase-following edges, ADR-0025's duration-0 lifetime). Found while building tickets 02 and 16: both need it, and a package cannot infer the phase from marker systems, because a phase's parallel systems all run before its sequential ones, and systems from packages installed later land on the wrong side of the marker. Settled with the user on 2026-10-22 as its own prerequisite, not part of 02 or 16.

- **`App::CurrentPhase() -> std::optional<SchedulePriority>`:** the phase whose systems are running. It is set before each phase starts, including each of the init phases and `Exit`, and is constant while the phase runs, so parallel and sequential systems read the same value. It is empty outside `Run`, `RunFrame` and `RunExit`. It is read-only. Not a scheduler rule: phases run as before.
- **`Time::frameCount` and `Time::tickCount`:** the frames and fixed steps begun so far, as 64-bit counts. The App increments `frameCount` as each frame begins (before `PreUpdate`) and `tickCount` as each fixed step begins (before its `PreTick`), so during a frame or a tick they number it, from 1. Both are 0 before the first frame. Like the rest of `Time`, they are the App's to write.
- A package stamps what it records with the count it was recorded under and compares stamps when read. FenrirInput's edges (02) and FenrirDebugDraw's duration-0 items (16) are built on this.

**Blocked by:** 01 (Engine prerequisites).

**Touches:** `packages/FenrirApp/`, `packages/FenrirScheduler/`, `packages/FenrirTime/`

**Status:** resolved

- [x] `CurrentPhase` names each phase from a system in that phase, parallel and sequential, for every phase including `PreInit`, `Init`, `PostInit` and `Exit`; it is empty before the first frame, between frames and after `RunExit`.
- [x] `frameCount` is 0 before the first frame and numbers each frame from 1 in every phase of it; `tickCount` numbers each fixed step from 1 in its `PreTick`, `Tick` and `PostTick`, stays at the last step's number through a frame with no tick, and grows by three in a frame with three ticks.
- [x] Clang and MSVC x64 debug and release presets build with no new warnings.
