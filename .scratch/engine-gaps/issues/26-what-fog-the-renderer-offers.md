# What fog does the renderer offer?

Type: grilling
Status: resolved
Blocked by: none
Settles: [No fog](14-fog.md)

## Question

Flightless Frenzy's open level, about 660 units wide, needs its far edges to soften into the sky ([What does the game look like?](../../flightless-frenzy/issues/12-the-look.md)); the plain kind, not volumetric light. The renderer map sketches both under "Post-processing beyond the lighting spec" (height fog with extinction and sky in-scattering, then shadowed sun shafts by froxels or ray march), never triaged.

- **What ships first:** distance fog, height fog, or both; its colour taken from the sky (the sky's average or a direction-dependent sky lookup) or set.
- **Where it lives:** a scene resource beside `Sky` and `SkyLight`, or per camera.
- **How it is applied:** in the forward material shading through `Fenrir/Lighting.glsl` (so a game's own material shader, like the toon shader, gets it with no work), as a pass after opaque, or both; how transparents and the skybox are fogged.
- **Volumetric light:** split off as a later spec on the renderer map, as its own question.
- **Where it is planned:** this map, or the renderer map's fog line graduated there.

Outcome: a spec-ready design.

## Answer

Grilled 2026-11-19 in round 3 of [Engine gaps for Flightless Frenzy](../map.md). The plain fog was accepted as recommended. **Volumetric fog was not left for later** (the user: it is worth exploring and most engines have it), so it moved into this effort as [How do engines do volumetric fog?](34-how-engines-do-volumetric-fog.md) and [What volumetric fog does the renderer offer?](35-what-volumetric-fog-the-renderer-offers.md). **The plain fog is spec-ready.**

- **What it is:** exponential distance fog plus exponential height fog, in a `Fog` scene resource beside `Sky` and `SkyLight`.
- **Its colour** is taken from the sky, with a settable override.
- **Opaque surfaces** are fogged by a full-screen pass after opaque that reads scene depth, so every material, the game's own toon shader included, is fogged with no work.
- **Transparents** are fogged in their own shading through a helper in `Fenrir/Lighting.glsl`, which the built-in transparent materials call and a game's transparent shader may call.
- **The sky** is not fogged.
- **Volumetric fog** sits beside this as its own tickets; it decides how the two combine.
