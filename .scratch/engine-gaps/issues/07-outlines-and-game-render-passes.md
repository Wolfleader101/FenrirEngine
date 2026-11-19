# A game cannot draw outlines or add a render pass

Status: needs-triage
Blocked by: none

## What is missing

A game's material shader can read the sun, lights, shadows and sky, which is enough for banded toon shading. It cannot read scene depth or normals, there is no stencil, and game code cannot add a post-process or any other pass to the frame. So screen-space outlines, the other half of a cel-shaded look, are out of reach. No toon material ships with the engine either; that part is game-side shader work and is not asked for here.

## Who needs it

Flightless Frenzy ([map](../../flightless-frenzy/map.md)), whose look is stylised and toon-shaded as its design document wanted. "What does the game look like?" decides whether outlines are part of it.

## Does anything plan it?

No. Only "a user pass in `Cull`" is named as a not-yet (`docs/adr/0057-*.md:40`).

## Does it block the game?

No. Banded shading without outlines is possible today. An inverted-hull outline drawn as a second material may also be possible if a material can cull front faces (not checked).

## Comments
