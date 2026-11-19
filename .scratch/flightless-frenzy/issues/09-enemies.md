# What are the enemies?

Type: grilling
Status: open
Blocked by: 01, 05, 06

## Question

Enemies are simple hazards: one or two kinds, defeated by a head stomp, and a hit costs time (settled while charting; no weapons, no health). Which kinds (the design document lists poachers, stoats, ferrets, weasels, cats, dogs, rats, snakes and laughing owls), and for each:

- how it moves (a fixed patrol, a chase when Kiki is near) and how it finds its way, given what the engine offers for paths and queries;
- what a hit does to Kiki besides the time cost (knock-back, a moment of no control) and how much time it costs;
- what a stomp does: the bounce Kiki gets, what happens to the enemy (ragdolls are specified for the engine but not yet built), whether it comes back;
- whether any kind cannot be stomped.

Path-finding or anything else the chosen behaviour needs and the engine lacks is an engine gap; say whether the game can ship with a simpler behaviour until it is closed.
