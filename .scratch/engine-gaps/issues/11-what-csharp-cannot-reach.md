# C# cannot switch scenes, stop the app or parent entities

Status: ready-for-agent
Blocked by: none

## What is missing

Not bound to C# today:

- **Entity parenting:** `Relationship` is read-only from C#, so a script cannot make one entity the child of another.
- **Scenes:** creating, switching and destroying scenes.
- **The app:** `App.Stop` (quitting goes through `Window.RequestClose`), the tick rate and elapsed time.

Registering a custom material shader and renderer configuration are also C++-only; the game keeps those in its C++ host and does not ask for them here. C#-defined resources are [their own issue](13-csharp-defined-resources.md).

## Who needs it

Flightless Frenzy ([map](../../flightless-frenzy/map.md)), whose gameplay and UI are all C# ([C++, C#, or a split?](../../flightless-frenzy/issues/04-cpp-csharp-or-a-split.md)):

- **Parenting:** a cage holding a friend, a pickup's effect following Kiki, a camera rig.
- **Scenes:** the title screen and the level are separate scenes, and a level restarts by destroying and recreating its scene, which resets its physics world.

## Does anything plan it?

No.

## Does it block the game?

Yes, in two steps. Parenting has no stand-in and likely blocks an early ticket. Scenes block the restart and title-screen tickets, not the first playable slice. It was agreed both are fixed in the engine, not worked around in the game or its host.

## Comments

- 2026-11-19, charting [Engine gaps for Flightless Frenzy](../map.md): settled by [How does C# parent entities?](18-how-csharp-parents-entities.md), [How does C# create, switch and destroy scenes, and stop the app?](19-how-csharp-reaches-scenes-and-the-app.md).
- 2026-11-19, round 2 of [Engine gaps for Flightless Frenzy](../map.md): spec-ready in [How does C# parent entities?](18-how-csharp-parents-entities.md) and [How does C# create, switch and destroy scenes, and stop the app?](19-how-csharp-reaches-scenes-and-the-app.md).
- 2026-11-19: the spec is written, [C# reach: parenting, scenes, the app, C# resources and mesh positions](../../fenrir-scripting-reach/spec.md), `ready-for-agent` for `/to-tickets`.
- 2026-11-19: tickets cut, the tickets of [C# reach: parenting, scenes, the app, C# resources and mesh positions](../../fenrir-scripting-reach/spec.md), `01` to `06` (stack 02, 03, 05, 06; 01 and 04 beside).
