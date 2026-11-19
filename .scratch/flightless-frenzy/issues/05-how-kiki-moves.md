# How does Kiki move?

Type: prototype
Status: resolved
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

## Answer

Played 2026-11-19 in the grey box on [`prototype/flightless-frenzy-movement`](https://github.com/Wolfleader101/FenrirEngine/tree/prototype/flightless-frenzy-movement) (`examples/KikiMovePrototype`). The user found it good as it stood and kept every default.

- **Camera: third person, locked.** It orbits a pivot 1.5 m above Kiki's feet at 6 m, from 70° down to 30° up, starting 20° down. Against walls, floors and ceilings it pulls in at once (a 0.25 m sphere cast, never closer than 0.6 m) and eases back out at 6 m/s. Under a ceiling the pivot drops first. It follows at 20/s across and 8/s up and down. Its field of view is 60° vertical. First person was built and compared, and it is not kept.
- **Kiki:** a capsule 1.0 m tall and 0.6 m across. It walks steps up to 0.3 m and slopes up to 45°.
- **Ground movement:** one speed, with no sprint. Top speed is 7 m/s, acceleration 60 m/s², and stopping and turning against the run 80 m/s². Facing follows the velocity at 900°/s.
- **The jump:**
  - A held jump rises 2.0 m in 0.38 s: rising gravity 27.7 m/s², launch speed 10.5 m/s.
  - Falling gravity is 1.8 × that (49.9 m/s²), and the maximum fall speed is 30 m/s. A held jump spends 0.68 s in the air on the flat.
  - Letting go while rising triples gravity, so a tap rises about 0.7 m.
  - Air control is 30 m/s², and air drag with no input 8 m/s².
  - Coyote time and the jump buffer are 0.12 s each.
- **The extra jump:** one more jump in the air, rising 1.5 m from wherever it starts.
- **The glide:** press jump in the air once any extra jump is spent, and hold it. A fall slows to a 2.5 m/s sink at 40 m/s², and the top speed across is 1.15 × the run's. It ends on letting go or landing.
- **The speed boost:** 1.5 × the run's top speed.
- **The numbers a level is built from:** these are worked out from the defaults, not measured in play. Gaps are edge to edge at equal heights, and the capsule's rounded edges add about 0.6 m to the plain airtime × speed. The first level's layout should confirm them in the grey box.

  | | Highest ledge | Longest gap |
  | --- | --- | --- |
  | Base jump | 1.5 m comfortably, 2.0 m only at the top of the arc | about 5 m (6 m using coyote time) |
  | Extra jump | about 3.5 m | about 8 m |
  | Speed boost | as the base jump | about 7.5 m |
  | Glide | as the base jump | about 9 m from a level jump; from a height, 3.2 m across for every metre dropped (about 34 m off a 10.5 m drop) |

- **Engine gaps:** none. `CharacterBody` did everything the grey box needed. Its state is one tick old, so game code that measures a takeoff uses the last grounded position. The camera's pull-in is game code over `PhysicsWorld::ShapeCast`.

## Comments

- **The prototype** is on branch [`prototype/flightless-frenzy-movement`](https://github.com/Wolfleader101/FenrirEngine/tree/prototype/flightless-frenzy-movement): one commit on main, the throwaway demo app `examples/KikiMovePrototype` on a `CharacterBody`. Its README has the build and run commands, the controls, the default numbers and the course's measured ledges (0.5 to 6.0 m), gaps (2 to 12 m), ramps, wall, ceilings and a 10.5 m tower. The numbers are starting points, not decisions: the answer comes from playing it. Building it found no gap in `CharacterBody`.
