# How are levels authored and loaded?

Type: grilling
Status: open
Blocked by: 01, 04

## Question

The spec has one level, built so that a second is new data and not new code (settled while charting). What is that data, and who or what writes it?

- Where level geometry is made (a modelling tool exporting a format the engine loads, or built from primitives in a file) and how its collision is made.
- How the things in a level are placed: Kiki's start, cages, coins, power-ups, enemies and their patrols, the exit, the fall boundary, lights and sky. Markers inside the model file, a separate level file, or code.
- How a level is loaded and unloaded, and what a restart reloads.
- How fast the loop from changing a level to playing it is.

The engine/game line applies: a level format that describes *this game's* things is the game's; a general scene file or serialisation the engine lacks is an engine gap (the `fenrir-serialisation` planning, if the inventory finds it, says what is coming). A world editor is out of scope.
