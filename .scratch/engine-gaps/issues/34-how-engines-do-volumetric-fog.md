# How do engines do volumetric fog?

Type: research
Status: open
Blocked by: none
Settles: [No fog](14-fog.md) (volumetric fog)

## Question

How do Unreal, Godot 4 and Unity HDRP do volumetric fog, and what do Hillaire's (Frostbite, 2015) and Wronski's (Assassin's Creed IV, 2014) techniques say: froxel volume or ray march, grid resolution and depth distribution, which lights scatter (the sun through its cascades, local lights, the sky), the phase function, temporal filtering and its artefacts, how transparents and particles are fogged, how local fog volumes are placed, published costs, how it combines with plain height fog, and what each needs from a renderer (compute, 3D textures, shadow maps in compute, history)? What does FenrirRenderer have for it today, beside the plain fog [What fog does the renderer offer?](26-what-fog-the-renderer-offers.md) decided?

Findings go in `.scratch/engine-gaps/research/volumetric-fog.md` on the `research/volumetric-fog` branch.
