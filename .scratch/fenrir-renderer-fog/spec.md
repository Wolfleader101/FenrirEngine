# Plain fog

Status: ready-for-agent

Charted by [Engine gaps for Flightless Frenzy](../engine-gaps/map.md): [What fog does the renderer offer?](../engine-gaps/issues/26-what-fog-the-renderer-offers.md). Settles the plain half of [No fog](../engine-gaps/issues/14-fog.md); volumetric fog is [What volumetric fog does the renderer offer?](../engine-gaps/issues/35-what-volumetric-fog-the-renderer-offers.md), a later spec of its own that builds on this one.

## Problem Statement

The renderer has no fog of any kind: far geometry never fades toward the sky by distance or by height. An open level's far edges end in a hard line against the sky, and a valley cannot hold low mist. Flightless Frenzy's level is about 660 units across and its far edges need to soften into the sky, and its rule is that it does not fake a renderer feature inside its own material shader, so fog has to come from the engine and reach the game's own toon shader with no work on the game's side.

## Solution

A `Fog` scene resource beside `Sky` and `SkyLight` turns on exponential distance fog and exponential height fog for the scene. Its colour comes from the sky's lighting in each pixel's view direction, unless the game sets a colour. Opaque surfaces are fogged by a full-screen pass after the background, reading scene depth, so every material is fogged, a game's own included. Transparents are fogged in their own shading through a helper in the shared lighting include, which the built-in transparent materials call and a game's transparent shader may call. The sky is not fogged. C# reaches the resource, and LightingDemo's Forest clearing shows it.

## User Stories

1. As a game developer, I want to add a fog resource to a scene, so that its far geometry fades toward the sky.
2. As a game developer, I want no fog when a scene has no fog resource, so that nothing changes until I ask for it.
3. As a game developer, I want distance fog with a density I set, so that the world thins into haze the farther it is.
4. As a game developer, I want a distance at which fog starts, so that the area around the player stays clear.
5. As a game developer, I want height fog that is thickest low and thins with height, so that valleys hold mist and peaks stand clear.
6. As a game developer, I want to set the height fog's base height and how fast it thins, so that I can place the mist in my level.
7. As a game developer, I want distance and height fog together, so that one resource gives haze and mist.
8. As a game developer, I want a cap on how much fog any surface takes, so that the farthest geometry stays faintly visible if I want.
9. As a game developer, I want fog coloured by the sky in the direction I look, so that it blends into the sky without my picking a colour.
10. As a game developer, I want to override the fog's colour, so that I can tint it for a mood.
11. As a game developer, I want an intensity on the fog's colour, so that I can brighten or darken it against the scene.
12. As a game developer, I want my own material shaders fogged with no change, so that my toon shader gets fog for free.
13. As a game developer, I want transparent surfaces fogged as the opaque surfaces behind them are, so that glass and particles do not glow out of the haze.
14. As a game developer writing a transparent shader, I want one helper call that fogs my colour, so that my transparents match the built-in ones.
15. As a game developer, I want the sky left unfogged, so that the skybox keeps its look.
16. As a game developer, I want fog to work the same with MSAA on, so that edges against the sky do not show a fringe.
17. As a game developer, I want fog applied before bloom and the tonemap, so that it is graded with the rest of the frame.
18. As a game developer, I want every camera view fogged, so that split screen and picture-in-picture match.
19. As a C# game developer, I want to read and write the fog resource from C#, so that my C++ host stays a host.
20. As a C# game developer, I want to change fog at run time, so that I can thicken it as the timer runs down.
21. As an engine maintainer, I want the fog maths tested on the CPU against a numerical march, so that the closed-form height fog is right.
22. As an engine maintainer, I want a golden image of fogged objects, so that a regression shows.
23. As an engine maintainer, I want LightingDemo's Forest clearing to show fog with keys to adjust it, so that I can check it by hand.
24. As an engine maintainer, I want the fog's values available to the later volumetric fog, so that it can feed the same density into its volume.

## Implementation Decisions

- **The `Fog` scene resource**, beside `Sky` and `SkyLight` and reached the same way from C++ and C# (ADR-0026, ADR-0047): distance density, start distance, height density, base height, height falloff, a maximum opacity, a colour mode (from the sky, or a set linear colour) and a colour intensity. A scene without one has no fog. It is described for the API description, so a later scene file can write it.
- **The fog amount** along a view ray from the camera to a surface is one minus the transmittance of distance fog (exponential in the distance past the start distance) times that of height fog (the closed-form integral of a density falling exponentially with height above the base height, with the near-horizontal case handled so it does not divide by zero), clamped to the maximum opacity. The same function serves opaque and transparent shading.
- **The fog colour** from the sky is the sky's irradiance spherical harmonics, which the frame already holds, evaluated in the view direction and scaled by the intensity; a set colour replaces it.
- **Opaque surfaces** are fogged by a renderer-core fog pass in every camera view, after the background is drawn and before transparents, reading scene depth: it blends the fog colour over scene colour by the fog amount and leaves the sky's pixels (at far depth) alone. With MSAA it runs per sample, reading the multisampled depth, so edges are fogged per sample with no resolve. It is a render feature of the core, not a game pass.
- **Transparents** are fogged in their own shading: the shared lighting include gains a fog helper taking a world position and a shaded colour, using the frame's fog values. The built-in transparent materials (lit and unlit, alpha and additive) call it; additive ones fade toward no contribution rather than toward the fog colour. A game's transparent shader calls the same helper.
- **The frame's uniforms** carry the fog values for both; with no `Fog` resource the pass is not added and the helper returns its input.
- **The sky** is not fogged.
- **For volumetric fog** later: the `Fog` resource's density values are the ones a volume will read; nothing here is shaped against it beyond that.
- **Glossary:** *Fog* is added to the rendering section: a scene's distance and height haze toward the sky, applied after opaque and in transparents' shading.

## Testing Decisions

- **A good test here checks what fog does to a pixel or to the frame's graph**, not how it is coded.
- **CPU maths tests** (prior art: the BRDF and spherical-harmonics tests, which test shared shader maths on the CPU): the distance fog amount; the height fog's closed form against a numerical march along rays at many angles, the near-horizontal case included; the combination and the opacity cap; the sky colour from spherical harmonics in a direction.
- **Null-backend renderer tests:** no fog pass without a `Fog` resource; with one, the fog pass runs after the background and before transparents in every camera view; the fog values reach the frame's uniforms; the built-in transparent materials' shaders use the fog helper.
- **A GL golden-image test** (label `gl`, skipped without a GPU): a row of objects receding into distance and height fog under a sky, with a transparent object in front, compared with a committed golden.
- **FenrirRendererScripting's fixture tests** (prior art: a `Sky` written from C# is what the background draws): a `Fog` written from C# is the fog the frame uses, and C++'s reads back in C#.
- **By hand, LightingDemo's Forest clearing:** fog on, with keys and panel rows for distance density, height density, height falloff, the colour override and fog on or off, listed in the scene's help. The scene needs the fetched assets, so its smoke run does not cover fog; the null and golden tests do.

## Out of Scope

- Volumetric fog, fog volumes and light shafts: their own spec.
- Fogging the sky, and fog that varies by region.
- Per-camera fog settings.
- Auto-exposure and anti-aliasing beyond MSAA, which share the renderer map's post-processing line.

## Further Notes

- The facts behind the decisions are in [What fog does the renderer offer?](../engine-gaps/issues/26-what-fog-the-renderer-offers.md) and the renderer map's post-processing line.
- The fog pass is the renderer's own; the game passes spec's point after opaque is a different thing, though both sit after the background.
- Build and test only `msvc-x64-debug` locally, run the GL tests on your own GPU before a PR that touches the OpenGL backend, and use the short-path junction in a worktree (`README.md`, "Building and testing").
