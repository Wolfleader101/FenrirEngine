# How does a system read each event exactly once?

Type: grilling
Status: open
Blocked by: 17

## Question

A reader of FenrirApp's events sees each event in two consecutive frames, and a `Tick` reader re-reads the current frame's buffer on each tick. That handles a discrete event (a contact began, a key was pressed) twice. Physics delivers contact and trigger events as engine events ("How do contact and trigger events reach game code?"), and the DemoApp's input handling already reads this way.

Using "How do other engines deliver events to their systems?":

- Was the two-frame window intended (parallel systems, events sent late in a frame), and which of those needs survive the fix?
- Which model: per-reader cursors (Bevy), events stamped with a frame or tick, separate queues for the fixed step, immediate observers, or a mix?
- How does it behave for readers in `Tick` (zero or several times a frame) and in `Update`, and for an event sent in `Tick` and read in `Update`?
- How does C# read with the same guarantee (a C# system is a class, so it can hold a cursor)?
- Can events become more flexible than byte-copyable plain data (the investigation the user asked for), and what would C# need for that?
- What changes for existing readers (DemoApp input, scripting's `Events.Read`)?

Likely an ADR: it changes a core engine contract.
