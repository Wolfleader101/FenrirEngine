# What volumetric fog does the renderer offer?

Type: grilling
Status: open
Blocked by: 34
Settles: [No fog](14-fog.md) (volumetric fog)

## Question

The user wants volumetric fog explored as a feature of the engine, beside the plain distance and height fog [What fog does the renderer offer?](26-what-fog-the-renderer-offers.md) decided. With [How do engines do volumetric fog?](34-how-engines-do-volumetric-fog.md) in hand:

- **The technique:** a froxel volume or a ray march (the renderer map says this choice wants an ADR), its resolution and depth distribution.
- **What scatters:** the sun through its shadow cascades (shafts between trees), local lights and their shadows, the sky's light.
- **Density:** global from the `Fog` resource, height falloff, local fog volumes placed as entities, and whether a game can write density (a fog shader).
- **How it meets the rest:** the plain fog (replaced, or combined), transparents and particles, temporal filtering against the existing scene history.
- **What it needs from the device:** compute passes, 3D textures, shadow maps read in compute.
- **Its cost and a quality setting,** and whether it is per camera.
- **Where its spec sits:** with the plain fog, or as a later spec of its own.

Outcome: a spec-ready design.
