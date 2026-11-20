# Decals

Status: ready-for-agent

Charted by [Engine gaps for Flightless Frenzy](../engine-gaps/map.md): [What is a decal?](../engine-gaps/issues/36-what-is-a-decal.md), split from [What is the engine's particle system?](../engine-gaps/issues/25-what-the-particle-system-is.md). Settles the decal half of [No particles or billboards](../engine-gaps/issues/06-particles-and-billboards.md).

## Problem Statement

The renderer cannot put a mark on a surface it did not model: a footprint, a scorch, a splash stain, a sign painted on a wall. The only way today is extra geometry laid over the surface or a material per marked surface, neither of which follows the surface's shape or comes and goes cheaply. Particles that land cannot leave anything behind. A forward renderer has no G-buffer to project marks into after the fact, so decals have to reach surfaces during their own shading.

## Solution

A `Decal` component projects a decal material into its entity's box onto the surfaces inside it, on the render layers it names. Decals are listed in the light clusters' cells, and the shared lighting function applies the decals covering each pixel to the pixel's surface (its base colour, normal, roughness and metal, and emission, each optional) before lighting it, so every material that shades through that function takes decals, a game's own included. Decal textures are packed into a decal atlas so many decals shade together. Decals fade with the angle of the surface and with distance, can fade out after a lifetime, and are capped per view. C# reaches the component, a particle can spawn a decal where it lands, and LightingDemo gains a Decals scene.

## User Stories

1. As a game developer, I want to place a decal as an entity with a box, so that a mark appears on whatever surface the box covers.
2. As a game developer, I want a decal to follow a surface's shape, so that a stain wraps over a rock.
3. As a game developer, I want a decal to change a surface's colour, so that I can draw a footprint.
4. As a game developer, I want a decal to change a surface's normal, so that a crack looks dented.
5. As a game developer, I want a decal to change roughness and metal, so that a wet patch shines.
6. As a game developer, I want a decal to add emission, so that a glowing rune lights a floor.
7. As a game developer, I want each of those to be optional per decal, so that a footprint touches only colour and normal.
8. As a game developer, I want a decal's opacity, so that marks can be faint.
9. As a game developer, I want a decal to fade on surfaces turned away from its projection, so that a floor mark does not smear down a wall.
10. As a game developer, I want a decal to fade with distance from the camera, so that far marks cost nothing and do not shimmer.
11. As a game developer, I want a decal to fade out over a lifetime and then go, so that footprints disappear.
12. As a game developer, I want a decal to take only the render layers I name, so that a splash on the floor does not land on Kiki.
13. As a game developer, I want decals to sort among themselves by an order I set, so that a newer mark goes over an older one.
14. As a game developer, I want decal materials with their own textures, so that each mark looks as I made it.
15. As a game developer, I want many decals to shade in one go, so that a trail of footprints stays cheap.
16. As a game developer, I want a cap on decals per view, with the farthest or oldest dropped first, so that a game spawning many cannot slow the frame without bound.
17. As a game developer, I want my own material shader to take decals by shading through the engine's surface function, so that my toon shader shows them.
18. As a game developer, I want a particle to spawn a decal where it lands, so that a splash leaves a stain.
19. As a game developer, I want decals to move with their entities, so that a mark can be placed and moved like anything else.
20. As a C# game developer, I want to create and edit decals from C#, so that my game places marks in its scripts.
21. As an engine maintainer, I want the projection and fades tested on the CPU, so that the maths is right.
22. As an engine maintainer, I want a golden image of a decal across a floor and a wall, skipping an object on another layer, so that a regression shows.
23. As an engine maintainer, I want a Decals scene in LightingDemo, so that I can check decals by hand.

## Implementation Decisions

- **The `Decal` component** on an entity: a decal material; the box is the entity's transform's unit cube, projecting along its local -Z; an opacity; an angle fade (the surface angle where fading starts and ends); a distance fade (start and end from the camera); an optional lifetime with a fade-out time, after which the renderer stops drawing it and reports it ended; a sort order; and `layers`, the render layers it lands on (the game passes spec's bitmask, default every layer).
- **A decal material** is a material type of its own with optional textures and factors for base colour, normal, roughness and metal, and emission, and which of them it writes. It is not drawn as geometry.
- **The decal atlas:** decal materials' textures are packed by the renderer into atlas textures (one per kind: colour, normal, roughness-metal, emission), repacked when a decal texture is added, changed or released, each decal carrying its atlas region. Mip levels are made with the atlas, padded so regions do not bleed.
- **Clustering:** decals are gathered each frame like lights, written into a decal buffer up to a per-view cap in the renderer config (farthest first, then oldest, dropped past it), and listed in each light cluster's cell they touch by the same clustering compute.
- **Applying them:** the shared lighting function, before lighting, finds the pixel's cluster, and for each decal in it whose layers include the drawn item's layers (carried from the draw record to the fragment stage), projects the pixel's position into the decal's box, and blends what the decal writes into the surface in sort order: colour by alpha, the normal by a reoriented blend in the surface's tangent space, roughness and metal by alpha, emission added. Built-in lit materials and any material shading through the function take decals; unlit materials and transparents do not.
- **Shadows and depth** are unchanged: decals affect shading only.
- **Particles:** once the particles spec is built, a particle effect's sub-emitter can be a decal (a decal material and size), spawned as an ordinary `Decal` entity under the cap at the particle's death or collision point, oriented by its velocity or the hit normal.
- **C#:** the `Decal` component and decal materials, through the generated bindings.
- **Shaders** changes go into the shared lighting include and the clustering compute in Slang (ADR-0073) where those are ported.
- **Glossary:** Decal is already defined.
- **No ADR:** it extends the clustered forward frame the way lights do (ADR-0049) and reverses nothing.

## Testing Decisions

- **A good test here checks what reaches a surface and the frame**, never how the clustering is coded.
- **CPU maths tests** (prior art: the BRDF and spherical-harmonics tests): a world position projected into a decal's box and atlas region; the angle and distance fades; the normal blend; sort order of several decals on one pixel.
- **Null-backend renderer tests:** decals are gathered and listed in the cells they touch; the per-view cap drops the farthest, then the oldest; decal textures are packed into the atlas and repacked when one changes; a decal's layers and the draw's layers reach the shading; a decal with a lifetime ends; with no decals the frame is unchanged.
- **A GL golden-image test** (label `gl`): a decal across a floor and a wall with angle fade, beside an object on a layer the decal skips, compared with a committed golden.
- **FenrirRendererScripting's fixture tests:** a `Decal` and a decal material written from C# reach the frame.
- **By hand, LightingDemo's new Decals scene:** keys spawn decals on the floor and walls, switch their layer mask, fade them and clear them; with the particles spec built, a burst leaves stains where it lands.

## Out of Scope

- Decals on transparents and unlit materials.
- Mesh decals (decal geometry laid on a surface).
- Decals that write depth or change silhouettes (parallax and displacement).
- A deferred G-buffer.

## Further Notes

- It needs the game passes spec's render layers (that spec's first ticket) built first, and the particles spec built for the particle hookup.
- The facts behind the decisions are in [What is a decal?](../engine-gaps/issues/36-what-is-a-decal.md).
- Build and test only `msvc-x64-debug` locally, run the GL tests on your own GPU before a PR that touches the OpenGL backend, and use the short-path junction in a worktree (`README.md`, "Building and testing").
