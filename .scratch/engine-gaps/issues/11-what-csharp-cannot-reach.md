# C# cannot switch scenes, stop the app, parent entities or add a shader

Status: needs-triage
Blocked by: none

## What is missing

Not bound to C# today:

- creating, switching and destroying scenes;
- `App.Stop` (quitting goes through `Window.RequestClose`), the tick rate and elapsed time;
- entity parenting: `Relationship` is read-only;
- registering a custom material shader (C# loads materials by `builtin:` name only), and renderer configuration;
- resources defined in C# ("left for later", `docs/adr/0029-*.md:42`).

## Who needs it

Flightless Frenzy ([map](../../flightless-frenzy/map.md)), if "C++, C#, or a split?" puts its gameplay in C#. Then it needs scene switching between the title and the level, parenting (a pickup's effect on Kiki, a cage's contents), a place for game state that is not a component (the timer, the score), and its toon material on the entities it spawns.

## Does anything plan it?

Only C#-defined resources, as "left for later" in ADR-0029.

## Does it block the game?

Only a C# game, and then in part. A small C++ host can switch scenes and register shaders on the game's behalf, and state can sit on a single entity. Parenting from C# has no such stand-in.

## Comments
