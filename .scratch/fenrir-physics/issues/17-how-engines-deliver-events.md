# How do other engines deliver events to their systems?

Type: research
Status: open
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
