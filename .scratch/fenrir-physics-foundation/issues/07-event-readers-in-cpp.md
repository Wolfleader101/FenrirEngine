# 07: Event readers in C++ (expand)

**What to build:** A C++ system reads each event exactly once through an `EventReader<T>` it owns (ADR-0012; spec: "Events"; glossary: event reader). Each queue counts its events; a reader remembers how far it read, yields only newer events as up to two ranges (never a copy into shared state), and reports how many it missed after two swaps without reading. The swap at the end of a frame happens only if a tick ran since the last swap. Sends from systems the scheduler runs in parallel are buffered per system and appended after the batch in registration order; queue lookup and creation are guarded. This is the expand step: `App::ReadEvents<T>()` still exists beside it.

**Blocked by:** 01 (App runs one frame at a time with a chosen delta); 02 (Fixed-step phases and a cap on ticks per frame).

**Status:** ready-for-agent

- [ ] Tests (frame seam): an `Update` reader sees each event once, including one sent after it ran (seen next frame, once); a `Tick` reader that runs three times in a frame sees each event on its first run after the send; a `Tick` reader never misses an event across frames that run no tick; a reader that skips two swaps reports the missed count.
- [ ] Events sent by two systems added with `AddSystems` (parallel) arrive in registration order, run after run (test).
- [ ] Parallel readers of one queue and parallel senders do not race (no shared mutable read buffer; per-system send buffers).
- [ ] `ReadEvents` and every existing reader keep working.
