# C# cannot define resources

Status: needs-triage
Blocked by: none

## What is missing

A resource is state that belongs to the app or to a scene and not to an entity. C++ can define and read them; a C# script cannot define one of its own. ADR-0029 names this as "left for later" (`docs/adr/0029-*.md:42`).

## Who needs it

Flightless Frenzy ([map](../../flightless-frenzy/map.md)), written in C# ([C++, C#, or a split?](../../flightless-frenzy/issues/04-cpp-csharp-or-a-split.md)): the run's timer, score and current state, and later its settings and records, belong to the scene or the app, not to any one entity.

## Does anything plan it?

Only the "left for later" line in ADR-0029.

## Does it block the game?

No. The game keeps this state as components on one "run" entity until C# can define resources, then moves it. The user wants the engine to gain this.

## Comments

- 2026-11-19, charting [Engine gaps for Flightless Frenzy](../map.md): settled by [How does C# define its own resources?](20-how-csharp-defines-resources.md).
