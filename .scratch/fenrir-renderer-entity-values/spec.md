# Per-entity material values

Status: ready-for-agent

Charted by [Engine gaps for Flightless Frenzy](../engine-gaps/map.md): [How does one entity get its own material parameters?](../engine-gaps/issues/27-how-one-entity-gets-its-own-material-parameters.md). Settles [One entity cannot have its own colour without its own material](../engine-gaps/issues/15-per-entity-material-colour.md).

## Problem Statement

`MeshRenderer` holds a mesh, a material, a visibility flag and a shadow flag, and nothing else. Giving one entity a different colour or parameter value than another with the same material means cloning the material, which also splits them out of automatic instancing and multiplies the materials a game has to manage. Only an instance list's copies can have their own colour. Flightless Frenzy draws each caged friend as the Kiki model in its own body colour, and turns Kiki's rim light to an active power-up's colour, and its rule is to get these from the engine rather than clone materials for ever.

## Solution

`MeshRenderer` gains a `colour` tint and four `custom` vec4 values. The tint fills the colour every draw record already carries, which the built-in shaders multiply their base colour by, and an instance list's copies multiply their own colour by it. The custom values join the draw record, and any material shader, a game's own included, reads both through the shared draw include. Because they live in each item's own record, entities with different values still merge into one instanced draw and are culled as before. C# reaches both fields, and RendererDemo gains a Tints scene.

## User Stories

1. As a game developer, I want to give one entity a colour tint, so that the same model and material can show different colours.
2. As a game developer, I want the tint to default to white, so that nothing changes until I set it.
3. As a game developer, I want the built-in lit and unlit materials to multiply their base colour by the tint, so that tinting works with no shader of my own.
4. As a game developer, I want my own material shader to read the tint, so that my toon shader colours a friend's body.
5. As a game developer, I want four custom vec4 values per entity, so that my shader can take values of my own, such as a rim colour.
6. As a game developer, I want custom values to default to zero, so that a shader can tell when they are unset.
7. As a game developer, I want entities with different tints and custom values to still instance together, so that a crowd of friends costs one draw.
8. As a game developer, I want them culled as before, so that per-entity values change nothing about what is drawn.
9. As a game developer, I want an instance list's copy colours multiplied by its entity's tint, so that I can tint a whole field of coins at once.
10. As a game developer, I want an instanced entity's custom values to apply to every copy, so that one value drives the whole list.
11. As a game developer, I want a model's body and eyes, which are separate entities, to take values separately, so that only a friend's body changes colour.
12. As a game developer, I want to change tints and custom values every frame, so that a power-up's glow can pulse.
13. As a game developer, I want tints and custom values to reach every pass that draws the entity, so that its shadow and depth passes agree with its colour pass where they read them.
14. As a C# game developer, I want to read and write `colour` and `custom` on `MeshRenderer` from C#, so that my game sets them in its scripts.
15. As an engine maintainer, I want instancing tests that prove values do not split merged draws, so that a later change cannot quietly cost draws.
16. As an engine maintainer, I want a GL test that reads back tinted pixels, so that the values reach the screen.
17. As an engine maintainer, I want a Tints scene in RendererDemo with a draw-count readout, so that I can see many tints in one draw by hand.

## Implementation Decisions

- **`MeshRenderer`** gains `colour` (a linear `Colour`, default white) and `custom` (four vec4 values, default zero). Both are described for the API description.
- **The draw record** keeps its existing `colour`, now filled from the entity's tint (it was always white for a `MeshRenderer`), and gains the four custom values, growing from 192 to 256 bytes in std430; the C++ mirror and its static checks follow. Every item written from a `MeshRenderer` (lone, merged, skinned) carries its entity's values.
- **`InstancedMeshRenderer`** gains the same two fields: the tint multiplies each copy's colour as the copy's colour multiplies the record's today, and the custom values apply to every copy. Instance list copies keep their own colour and gain no custom values.
- **Shaders** read both through the shared draw include: the existing colour and a new accessor for the custom values on the current draw. The built-in lit and unlit programs already multiply by the colour and need no change beyond it being filled.
- **Instancing and culling** are unchanged: items merge by mesh, material and pipeline as before, each instance its own record, so different values never split a run (ADR-0051), and the cull reads the same record (ADR-0057).
- **Named overrides of any material parameter** and per-entity textures are deferred, as decided.
- **C#:** `colour` and `custom` on `MeshRenderer` and `InstancedMeshRenderer`, through the generated bindings.
- **No ADR:** it fills a field the record already has and adds four values, and reverses no decision.

## Testing Decisions

- **A good test here checks draws and pixels**: how many draws and instances, what each record holds, what colour reaches the screen; never how the record is packed.
- **Null-backend instancing tests** (prior art: the automatic instancing tests through `App::RunFrame`): entities sharing a mesh and material with different tints and custom values still merge into one instanced draw in DepthPrepass and Opaque; each record carries its entity's values; an instance list's copy colour is multiplied by its entity's tint; the defaults leave existing scenes' records unchanged.
- **A GL draw test** (prior art: the OpenGL draw tests, label `gl`): three copies of one mesh in different tints, and a test material shader that writes a custom value as colour, checked by reading back pixel colours.
- **FenrirRendererScripting's fixture tests:** `colour` and `custom` written from C# reach the draw record.
- **By hand, RendererDemo's new Tints scene:** a grid of one mesh in many tints, one custom value driving a pulse in a small demo shader, a key to shuffle the tints, and a readout of the draw count showing the grid stays one draw.

## Out of Scope

- Named overrides of any material parameter, and per-entity textures.
- Per-copy custom values on instance lists.
- Material property animation tracks.

## Further Notes

- The facts behind the decisions are in [How does one entity get its own material parameters?](../engine-gaps/issues/27-how-one-entity-gets-its-own-material-parameters.md).
- If the Slang move (ADR-0073) has ported the shared draw include by the time this is built, the accessor goes in its Slang module.
- Build and test only `msvc-x64-debug` locally, run the GL tests on your own GPU before a PR that touches the OpenGL backend, and use the short-path junction in a worktree (`README.md`, "Building and testing").
