# How does the game build against the engine?

Type: grilling
Status: open
Blocked by: 01

## Question

The game lives in `games/FlightlessFrenzy/` in this repo, apart from `examples/`, and builds against the engine as an outside program would (settled while charting). How, exactly?

- Which CMake targets it links, and whether it is part of the root build behind an option or a project of its own that finds the engine.
- Whether it may use anything from `examples/DemoScaffold` or `examples/assets`, or only public engine packages.
- Where its C# project sits and how it joins `FenrirScripting.sln`, if the game has C# ("C++, C#, or a split?" decides that; this ticket covers both outcomes or waits for it).
- Where its assets live and how they reach the built program.
- Whether CI builds it and runs its tests.
- What would have to change to move it to its own repo later; anything that makes that hard is an engine gap.
