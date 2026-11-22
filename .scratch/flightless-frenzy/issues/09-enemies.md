# What are the enemies?

Type: grilling
Status: resolved
Blocked by: 01, 05, 06

## Question

Enemies are simple hazards: one or two kinds, defeated by a head stomp, and a hit costs time (settled while charting; no weapons, no health). Which kinds (the design document lists poachers, stoats, ferrets, weasels, cats, dogs, rats, snakes and laughing owls), and for each:

- how it moves (a fixed patrol, a chase when Kiki is near) and how it finds its way, given what the engine offers for paths and queries;
- what a hit does to Kiki besides the time cost (knock-back, a moment of no control) and how much time it costs;
- what a stomp does: the bounce Kiki gets, what happens to the enemy, whether it comes back;
- whether any kind cannot be stomped.

Path-finding or anything else the chosen behaviour needs and the engine lacks is an engine gap; say whether the game can ship with a simpler behaviour until it is closed.

## Answer

Grilled 2026-11-22. No engine gap came out of it. Every distance, speed and time here is a starting value to tune.

- **Two kinds.** The stoat is built first.

  | | The stoat | The poacher |
  | --- | --- | --- |
  | What it is | small and quick | big and slow, the story's villain |
  | Where | roams a patrol | walks a beat near a cage |
  | Chases | yes | no: turns to face Kiki and lunges when she is within 2 m |
  | A stomp | defeats it | bounces Kiki off his hat and dazes him for 3 seconds |

  The poacher cannot be defeated. This amends "defeated by a head stomp", settled while charting, for the poacher only.
- **How they move:** both walk between patrol points in straight lines. The stoat chases Kiki in a straight line at 5.5 m/s when she is within 8 m and in sight. It gives up when she is beyond 12 m, out of sight for 3 seconds, or its next step would take it off a ledge, and then walks back to its patrol. Kiki runs at 7 m/s, so a stoat can always be outrun.
- **No path-finding.** Sight and the ledge check are ray casts. The engine's navigation stays deferred ([No navigation or path-finding](../../engine-gaps/issues/08-navigation.md)).
- **Being hit:** 5 seconds off the timer; Kiki is knocked back and up, has no control for half a second, and blinks and cannot be hit for 1.5 seconds. The blink is Kiki shown and hidden.
- **The fall penalty,** left as "a fixed number" in "What is the game loop?", is also 5 seconds to start with.
- **The stomp:** landing on an enemy from above while falling. Kiki bounces 1.2 m, or the full 2.0 m if jump is held, and gets her extra jump back if that power-up is active. Touching an enemy any other way is a hit.
- **A stomped stoat** squashes flat, vanishes and scores 200. It stays gone until the level restarts, so it cannot be farmed for score.
- **Their models:** the 2022 art has none. They are found under the free-to-share licences or modelled simply in the style of Darcy Wilson's art, and moved as rigid models in code at first (a hop, a wobble), as Kiki is. The first playable slice uses plain placeholder shapes until they exist. Who makes them is decided with the Kiki rigging and feather tickets.

Where patrol points come from is for "How are levels authored and loaded?". The rules here (when to chase and give up, what counts as a stomp, the penalties) are plain C# types under unit test.
