# How do other engines deliver events to their systems?

Type: research
Status: resolved
Blocked by: none

## Question

FenrirApp's events sit in queues with two buffers: `ReadEvents` returns the previous and the current frame's events, and the queues swap once per frame (`App::UpdateEvents`, after `PostUpdate`). So a reader that runs every frame sees each event twice, and a reader in the fixed-step `Tick` phase (zero or several times a frame) re-reads the current buffer on each tick. The design may have been meant to support parallel systems and events sent late in a frame (after a reader ran) without losing them. Events must also be byte-copyable so C# can read them through the same queue.

Survey, from primary sources (source code, official docs), how engines deliver events or messages between systems and to game code:

- **Bevy:** `Events<T>` / `Messages<T>` with per-reader cursors (`EventReader`, `MessageReader`), the double buffer and when it updates, what happens to events across fixed-timestep runs (`FixedUpdate`), and how parallel systems read the same events. Also observers/triggers.
- **Unity:** `OnCollisionEnter`-style callbacks and when they fire relative to `FixedUpdate`, the input event model (new Input System), and DOTS/Entities (how ECS code receives collision events: `ICollisionEventsJob`, the simulation's event streams).
- **Godot:** signals (immediate vs deferred connections), and how physics signals relate to `_physics_process`.
- **Flecs and EnTT:** EnTT's `dispatcher` (queued vs immediate), Flecs observers and events.
- **Unreal:** delegates and the hit/overlap events, briefly.

For each: when an event becomes visible, how long it stays readable, whether each reader sees it exactly once and how that is guaranteed, how it behaves with a fixed timestep running several times per frame, how parallel readers are handled, and what payloads are allowed (plain data only, or arbitrary types; how scripting languages receive them).

Close with the options these suggest for FenrirApp (for example per-reader cursors, events stamped with a frame or tick, separate queues for the fixed step, immediate observers), and their costs, without choosing.

Write findings to a Markdown file on the `research/engine-event-delivery` branch, citing each claim's source, and link it here.

## Answer

Findings: `.scratch/fenrir-physics/research/engine-event-delivery.md` on branch `research/engine-event-delivery` (commit `08b847f`). Pinned: Bevy v0.20.0, Godot 4.7.2-stable, EnTT v3.16.0, Flecs v4.1.6, Unity ECS samples `6786a741`. Gaps: Unity's callback timing rests on the simulation-mode docs; Unreal's hit/overlap timing is not covered.

- **FenrirApp today:** a reader after the sender sees an event twice (this frame and next), before the sender once. A `Tick` reader sees it once per tick after the send plus once per tick next frame, which is zero when a frame runs no tick (events lost at high frame rates) and several at low ones. An `Update` reader gets several ticks' events merged with no way to tell them apart.
- **The window does not make parallel systems safe:** `ReadEvents` rewrites a shared mutable buffer, `Send` pushes without a lock, and queues are created lazily in an `unordered_map`; all race when `AddSystems` runs systems in parallel.
- **The queue resembles Bevy 0.12's `Events` without its per-system cursor**, which is what gives Bevy exactly-once reading (inference).
- **Existing readers:** DemoApp window input is read once only because `glfwPollEvents` runs at the end of `PostUpdate`; AutoPlay's key sends are seen twice; `LogBursts` already hand-rolls a cursor.
- **Bevy:** a cursor per reader (exactly once); parallel readers, exclusive writers enforced by the scheduler; the buffer swap waits until a fixed step has run, so fixed-step readers never miss. Observers run immediately and store nothing.
- **Unity:** collision callbacks fire per fixed step, once per contact change, data valid only in the callback. DOTS event streams last one step and are read inside the fixed group; readers outside it miss steps. Input tracks "this frame" per update type.
- **Godot:** physics signals are emitted once per change at the start of the next physics step; input "just pressed" is stamped with a frame number per loop. Payloads are Variants; C# receives them through `[Signal]` delegates.
- **EnTT, Flecs, Unreal:** EnTT `trigger` is immediate, `enqueue` delivers at `update` and erases; Flecs observers run at the operation or a sync point (a plain queue is faster for simple cases); Unreal delegates broadcast immediately.
- **Options for FenrirApp** (not chosen): per-reader cursors; frame or tick stamps; separate fixed-step queues; immediate observers; an EnTT-style queue drained by an explicit update. Costs that apply whatever the choice: the concurrency fixes, what C# needs for richer payloads, and changes to existing readers and the `EventsHaveTheSameFrameWindowInBothLanguages` test.
