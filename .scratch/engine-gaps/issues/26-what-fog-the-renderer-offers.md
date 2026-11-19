# What fog does the renderer offer?

Type: grilling
Status: open
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
