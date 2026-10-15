# How do contact and trigger events reach game code?

Type: grilling
Status: resolved
Blocked by: 01, 04

## Question

How are collisions reported to systems (C++ and C#)?

- Delivered as engine events (the app's two-buffer event queues), as components on the entities, or both?
- Which events: contact begin, stay, end; trigger enter, exit? Is *stay* reported every tick or not at all?
- Payload: the two entities, contact points, normal, impulse? Kept byte-copyable so C# can read them through the existing event mechanism.
- Jolt's listeners run on its worker threads during the step: how are contacts buffered and turned into events after the step, in a deterministic order?
- Opt-in per body (only bodies that ask for events report them), or every contact?
- How do events interact with ticks: several ticks in one frame, and the event queue's one-frame window?

## Answer

- **Delivered as engine events** (`app.ReadEvents<ContactBegin>()`, and the same queue from C#), the ECS-data-driven idiom (Avian and bevy_rapier send messages too). Rejected: per-world lists on `PhysicsWorld` (a reader in `Update` misses a step when two ticks run in a frame, and C# would need its own path) and components on entities (a per-entity contact buffer needs variable-length data).
- **Reading each event once is an engine question, not a physics one.** Today `ReadEvents` returns the previous and current frame's buffers and the queues swap once per frame, so a reader that runs every frame sees each event twice, and a `Tick` reader re-reads the growing current buffer on each tick. The window may have been meant for parallel systems and for events sent late in a frame. See "How do other engines deliver events to their systems?" and "How does a system read each event exactly once?". The physics events below are decided regardless.
- **Events:** `ContactBegin`, `ContactEnd` (solid contacts); `TriggerEnter`, `TriggerExit`. No *stay* events; "what is this body touching now" is a query on the world (`world.ForEachContact(entity, fn)`).
- **Payloads** (plain data; entities mapped for C# as `Relationship`'s are):
  - `ContactBegin`: `bodyA`, `bodyB` (rigid body entities), `colliderA`, `colliderB` (collider entities, which differ inside a compound body), `point` and `normal` (world space, normal from A to B), `relativeSpeed` (closing speed along the normal; replaces impulse, which the backend does not know when a contact begins).
  - `ContactEnd`: the four entities.
  - `TriggerEnter`, `TriggerExit`: `trigger`, `otherBody`, `otherCollider`.
- **Opt-in:** contacts are reported only for pairs where either body has the `ContactEvents` tag component. Triggers always report.
- **Sleep is hidden** (promised behaviour, pinned by tests per ADR-0010): a contact ended by a body falling asleep is not a `ContactEnd`; the contact lasts until the bodies separate or one is removed. Triggers are kinematic sensors kept active, so a sleeping body inside one does not `TriggerExit`.
- **Order and timing:** callbacks buffer into per-thread lists during the step; after the step FenrirPhysics merges them, sorts by (entity A, entity B, collider A, collider B) for determinism, and sends them, once per step, in `Tick`. Removing a body or trigger (destroying its entity or removing its component) sends the matching `ContactEnd` / `TriggerExit` on the next step, naming the stale entity (its version detects it), so every begin has an end.
