# How does a system read each event exactly once?

Type: grilling
Status: resolved
Blocked by: 17

## Question

A reader of FenrirApp's events sees each event in two consecutive frames, and a `Tick` reader re-reads the current frame's buffer on each tick. That handles a discrete event (a contact began, a key was pressed) twice. Physics delivers contact and trigger events as engine events ("How do contact and trigger events reach game code?"), and the DemoApp's input handling already reads this way.

Using "How do other engines deliver events to their systems?":

- Was the two-frame window intended (parallel systems, events sent late in a frame), and which of those needs survive the fix?
- Which model: per-reader cursors (Bevy), events stamped with a frame or tick, separate queues for the fixed step, immediate observers, or a mix?
- How does it behave for readers in `Tick` (zero or several times a frame) and in `Update`, and for an event sent in `Tick` and read in `Update`?
- How does C# read with the same guarantee (a C# system is a class, so it can hold a cursor)?
- Can events become more flexible than byte-copyable plain data (the investigation the user asked for), and what would C# need for that?
- What changes for existing readers (DemoApp input, AutoPlay, `LogBursts`'s hand-rolled cursor, scripting's `Events.Read`, the `EventsHaveTheSameFrameWindowInBothLanguages` test)?
- The research found the queues race when `AddSystems` runs systems in parallel (shared `combinedBuffer`, unlocked `Send`, lazy queue creation). Which concurrency rule fixes that: readers in parallel and writers exclusive (Bevy), locking, or per-system send buffers?

Likely an ADR: it changes a core engine contract.

## Answer

Recorded as ADR-0012, `docs/adr/0012-each-event-reader-sees-each-event-once.md`.

- **What the window was for:** the queue copied Bevy 0.12's two buffers without its per-reader cursor. The one property worth keeping is that an event sent after a reader ran still reaches it next frame; the window never made parallel systems safe.
- **Model: per-reader cursors over the existing two buffers** (Bevy). Each queue counts its events; an `EventReader<T>` remembers where it read up to and yields only newer events, as up to two ranges, and reports how many it missed after two swaps without reading. Late sends are seen once next frame; a `Tick` reader running several times in a frame sees each event on its first run after the send. Rejected: frame or tick stamps (a rule per sending phase), separate fixed-step queues (`Update` readers still need a cursor), immediate callbacks (poor for high-volume events, need world-access rules, one C# crossing per event).
- **C++:** a system owns its `EventReader<T>` (a member, or captured in its lambda). `App::ReadEvents<T>()` is removed; `CameraController`, `Editor`, `Window` and the scripting bindings migrate, and `LogBursts`' hand-rolled count goes.
- **The swap waits for the fixed step:** queues swap at the end of a frame only if at least one tick ran since the last swap, so fixed-step readers never miss an event at high frame rates.
- **Parallel safety and determinism:** reads never copy into shared state; sends from systems run in parallel go into a buffer per system, appended after the batch in registration order (deterministic, no hot-path lock); sequential systems send directly; queue lookup and creation are guarded.
- **C#:** a system holds `private EventReader<T> name;` and reads with `foreach (var e in name.Read())`; sends still apply when the system returns. `EventsHaveTheSameFrameWindowInBothLanguages` becomes "each reader sees each event once, in both languages", across `Update` and `Tick` readers and late sends.
- **Payloads stay plain data;** anything richer is referenced by entity or handle. Vector-like data is the separate task "Investigate vectors in C# scripting components".
