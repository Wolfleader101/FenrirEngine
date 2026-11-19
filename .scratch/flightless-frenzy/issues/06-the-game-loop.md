# What is the game loop?

Type: grilling
Status: open
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
