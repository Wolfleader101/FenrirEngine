# How do contact and trigger events reach game code?

Type: grilling
Status: open
Blocked by: 01, 04

## Question

How are collisions reported to systems (C++ and C#)?

- Delivered as engine events (the app's two-buffer event queues), as components on the entities, or both?
- Which events: contact begin, stay, end; trigger enter, exit? Is *stay* reported every tick or not at all?
- Payload: the two entities, contact points, normal, impulse? Kept byte-copyable so C# can read them through the existing event mechanism.
- Jolt's listeners run on its worker threads during the step: how are contacts buffered and turned into events after the step, in a deterministic order?
- Opt-in per body (only bodies that ask for events report them), or every contact?
- How do events interact with ticks: several ticks in one frame, and the event queue's one-frame window?
