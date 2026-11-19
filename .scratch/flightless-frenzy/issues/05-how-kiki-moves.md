# How does Kiki move?

Type: prototype
Status: open
Blocked by: 01, 04

## Question

What do running, jumping, the extra jump, gliding and the camera feel like? Build a throwaway grey-box on the engine's character body: a flat floor, some platforms at measured heights and gaps, a capsule or placeholder kiwi.

Settle by playing it:

- **Camera:** third person is the working intent (settled while charting). Compare it against first person in the same grey-box before locking it. For third person: distance, height, vertical limits, what it does against walls and under platforms.
- **Ground movement:** top speed, acceleration and stopping, turning, whether there is a sprint or one speed.
- **The jump:** height, time in the air, gravity up and down, air control, held-jump height, the forgiveness windows (jumping just after leaving a ledge, pressing just before landing).
- **The extra jump and the glide:** how each feels on top of the base jump, since both arrive as timed power-ups.
- **The numbers a level is built from:** the highest ledge and the longest gap Kiki clears with and without each power-up.

The answer records the chosen camera and the numbers; the prototype stays on a `prototype/` branch as a pointer. Anything the character body cannot do becomes an engine gap.

## Comments

- **The prototype** is on branch [`prototype/flightless-frenzy-movement`](https://github.com/Wolfleader101/FenrirEngine/tree/prototype/flightless-frenzy-movement): one commit on main, the throwaway demo app `examples/KikiMovePrototype` on a `CharacterBody`. Its README has the build and run commands, the controls, the default numbers and the course's measured ledges (0.5 to 6.0 m), gaps (2 to 12 m), ramps, wall, ceilings and a 10.5 m tower. The numbers are starting points, not decisions: the answer comes from playing it. Building it found no gap in `CharacterBody`.
