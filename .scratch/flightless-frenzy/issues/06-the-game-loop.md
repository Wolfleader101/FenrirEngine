# What is the game loop?

Type: grilling
Status: resolved
Blocked by: 02

## Question

What are the game's states and rules from launch to the results screen?

- **States:** title, playing, paused, won, lost, and the moves between them; restart and quit.
- **The timer:** its length, when it starts, what happens at zero. A hit from an enemy costs time, not health (settled while charting): what else adds or removes time?
- **Friends and the exit:** how a caged friend is freed (touching the cage, as shipped, or something more), how the player knows how many remain, when the exit opens and how that is shown.
- **Falling:** what happens when Kiki falls off the level (respawn where, at what cost).
- **Score:** what earns points (coins, friends, time left, enemies), and whether there is a rank or medal.
- **Records:** whether best times and scores are kept between runs. If so and the engine has no way to save them, that is an engine gap.

Use the old game's values from "What did the old game ship?" as the starting point.

## Answer

Grilled 2026-11-19. The design document is the source of truth; where it is silent, the old code's numbers are starting values to tune, nothing more.

**The loop:** free every friend, then reach the exit before the timer runs out. Running out of time is the only way to lose.

- **States:**
  - Title → Playing
  - Playing ⇄ Paused
  - Playing → Won, or → Lost
  - From Paused, Won and Lost: Restart or back to Title
  - Quit from Title and from Paused

  Restart always reloads the whole level from the beginning. There is no mid-level save.
- **The timer** counts down in real seconds from the level's own time limit, which is a property of the level (about five minutes) and is shown as MM:SS. It starts on the player's first input, not when the level appears. At zero the run is lost at once. There are no lives and no health.
- **What changes the time**, and nothing else:
  - an enemy hit removes seconds (the amount is set in "What are the enemies?");
  - a fall removes a fixed number of seconds;
  - the slowdown power-up runs the timer at half speed while active.

  Nothing adds time.
- **Freeing a friend:** Kiki touches the cage; there is no button. The cage opens, the friend leaves, and the HUD's count of friends left goes down. What the friend does next stays in the map's fog.
- **Falling:** Kiki respawns at the last cage she opened, or at the start if she has opened none, and loses the fall penalty. Opened cages are the only checkpoints.
- **The exit** is visibly shut until the last friend is freed. It then visibly opens and the HUD tells the player to head for it. Touching it while open stops the timer and wins the run. Coins can still be collected after the last friend is freed.
- **Score**, as starting values to tune:

  | For | Points |
  | --- | --- |
  | each friend freed | 1000 |
  | a coin, by tier | 300, 500 or 700 |
  | each enemy stomped | 200 |
  | each second left on the clock, on a win only | 10 |

  There is no rank or medal. The results screen shows the score, the time left, and friends and coins found out of the total.
- **Records:** the game keeps the best score and the best time for the level between runs, in a small file it writes itself with .NET's own file and JSON support. It needs nothing from the engine for this, so the save-data half of [No scene or prefab files, and no way to save data](../../engine-gaps/issues/03-scene-files-and-save-data.md) does not hold the game up. Where the file lives on disk is for the spec.

**What this asks of the engine,** all already raised:

- Paused needs [pause and time scale](../../engine-gaps/issues/05-pause-and-time-scale.md).
- Restart and the title screen need scenes from C#: [C# cannot switch scenes, stop the app or parent entities](../../engine-gaps/issues/11-what-csharp-cannot-reach.md).

The half-speed timer is the game's own clock running slow, not the engine's time scale. The rules here (the timer, the penalties, the score, the state moves) are plain C# types under unit test, as "C++, C#, or a split?" decided.
