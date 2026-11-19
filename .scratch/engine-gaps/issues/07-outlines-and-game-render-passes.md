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

Yes: it must be resolved before the game is complete (the user, 2026-11-19). Outlines are part of the game's look ([What does the game look like?](../../flightless-frenzy/issues/12-the-look.md)) and are to come from the engine, not from a trick in the game's meshes. It does not block the first playable slice: banded shading without outlines works today.

Checked 2026-11-19: an inverted-hull outline drawn as a second material is not possible either. A material can set only its blend mode and `doubleSided`; it cannot cull front faces (`packages/FenrirAssets/include/FenrirAssets/MaterialData.hpp:93-107`).

## Comments
