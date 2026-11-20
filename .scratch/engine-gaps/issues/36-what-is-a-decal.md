# What is a decal?

Type: grilling
Status: resolved
Blocked by: none
Settles: [No particles or billboards](06-particles-and-billboards.md) (decals)

## Question

[What is the engine's particle system?](25-what-the-particle-system-is.md) made decals their own block, usable without particles (footprints, scorch marks) and spawnable by a particle. Round 4 accepted a `Decal` component that projects a material into a box, clustered like lights in the forward frame. Design it:

- **What a decal writes:** albedo only, or normals, roughness and emission too, and how that reaches a forward-shaded surface (clustered decals read during shading, as lights are, so every material including a game's own gets them through `Fenrir/Lighting.glsl`).
- **Its material:** a decal material type, its textures (an atlas for many decals in one cluster pass), and its blend.
- **Which surfaces take it:** a layer mask (the `layers` bitmask of [How does a game draw outlines and add its own render pass?](23-how-a-game-draws-outlines-and-adds-a-pass.md)), so a decal can skip characters; angle fade, distance fade.
- **Lifetime and count:** a cap, fading out, and what a particle's spawned decal costs.
- **The C# surface.**
- **Where its spec sits:** its own spec (`.scratch/fenrir-renderer-decals/`), as round 4 decided.

Outcome: a spec-ready design.

## Answer

Grilled 2026-11-19 in round 5 of [Engine gaps for Flightless Frenzy](../map.md); every recommendation was accepted. **Spec-ready**; its own spec, `.scratch/fenrir-renderer-decals/`.

- **What a decal is:** a `Decal` component projecting a decal material into its box, clustered like lights in the forward frame and read during shading through `Fenrir/Lighting.glsl`, so every material that shades through the engine's surface function gets decals; a game's own material shader (the toon shader) gets them if it goes through it.
- **What it writes:** albedo, normal, roughness and metal, and emission, each optional.
- **Its material:** a decal material type whose textures are packed in a decal atlas, so many decals shade in one pass; blended by alpha.
- **Which surfaces take it:** a `layers` mask choosing which render layers receive it (footprints skip characters), plus angle fade and distance fade.
- **Lifetime and count:** a cap on decals per view and an optional fade-out lifetime; a decal a particle spawns is an ordinary `Decal` entity under the same cap.
- **The C# surface:** the component's fields.
