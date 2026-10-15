---
status: accepted
---

# Each event reader sees each event once

FenrirApp's event queues keep two buffers and swap them once per frame, and `App::ReadEvents<T>()` returned both. So a system that read every frame saw each event twice, and a reader in the fixed-step `Tick` phase saw an event once per tick after the send (none in a frame that ran no tick, several in one that ran many). Discrete events (a key pressed, a contact began) were handled twice or lost. The queue had been copied from Bevy's `Events`, without the per-reader cursor that gives Bevy its guarantee. The two-frame window did keep one useful property: an event sent after a reader ran still reaches it next frame. It did not make parallel systems safe (`ReadEvents` rewrote a shared buffer, `Send` was unlocked, queues were created lazily). A survey of Bevy, Unity, Godot, EnTT, Flecs and Unreal is on branch `research/engine-event-delivery`.

We keep the two buffers and add what was missing:

- **A cursor per reader.** Each queue counts the events sent to it; an `EventReader<T>` remembers the count it last read up to, and a read yields only the events after it, as up to two ranges (never a copy into shared state). It also reports how many it missed, if it went two swaps without reading. A system owns its readers (a member, or captured in its lambda). C# systems hold the same reader as a field. `App::ReadEvents<T>()` is removed, not kept beside it as a trap.
- **The swap waits for the fixed step.** The queues swap at the end of a frame only if at least one tick has run since the last swap, so `PreTick`/`Tick`/`PostTick` readers never miss an event in a frame with no tick.
- **Parallel sends are buffered per system** and appended to the queue after the parallel batch in the order the systems were registered, so the order is deterministic (a standing rule: the same inputs give the same results on one machine and build). Sequential systems send directly. Queue lookup and creation are guarded.
- **Payloads stay plain data.** Anything richer is referred to by entity or handle, as contact events already do.

## Considered options

- **Stamping events with a frame or tick** (Godot's input): "exactly once" holds only if each reader runs exactly once per stamp, so every sending phase needs a rule for which tick it stamps.
- **Separate queues for the fixed step** (Unity Physics): `Update` readers of a per-tick queue still need a cursor to accumulate ticks.
- **Immediate callbacks or observers** (Bevy observers, EnTT `trigger`, Godot signals): every surveyed engine calls them a poor fit for high-volume events such as per-step contacts, they need world-access rules Fenrir lacks, and C# would cross the boundary once per event.
- **Locking each queue:** fixes the races but not the order of parallel sends.

## Consequences

- Every existing reader migrates: the DemoApp's `CameraController`, `Editor` and `Window`, the scripting bindings, and `LogBursts`, whose hand-rolled count becomes unnecessary.
- `EventsHaveTheSameFrameWindowInBothLanguages` is replaced by a test that each reader sees each event once in both languages, across `Update` and `Tick` readers and late sends.
- An `Update`-phase event lives longer when frames run no tick; a reader that reads at least once per swap still sees it exactly once.
