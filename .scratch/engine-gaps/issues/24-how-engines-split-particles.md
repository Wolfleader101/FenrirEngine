# How do engines split particles between the engine and the game?

Type: research
Status: resolved
Blocked by: none
Settles: [No particles or billboards](06-particles-and-billboards.md)

## Question

What do Godot 4, Unity (Shuriken, VFX Graph), Unreal (Niagara), Bevy (`bevy_hanabi`) and one lighter engine offer for particles: the data model, CPU or GPU simulation, one-shot bursts triggered from game code, billboard, mesh and trail rendering, sorting and blending with transparents, lighting, collision, and authoring? What did each ship first? What does FenrirRenderer offer today (instance lists with per-copy colour, the transparent pass, blend modes)?

Findings go in `.scratch/engine-gaps/research/engine-particles.md` on the `research/engine-particles` branch.

## Answer

Researched 2026-11-19: `.scratch/engine-gaps/research/engine-particles.md` on the local branch `research/engine-particles` (b2adff0e), never merged (`git show research/engine-particles:.scratch/engine-gaps/research/engine-particles.md`).

- **One split everywhere.** Godot, Unity's Shuriken and VFX Graph, Niagara, `bevy_hanabi` and Defold all have the engine own the particle runtime (pool, capacity, spawn, update, death, bounds, drawing) and a vocabulary of behaviours, and the game own the effect, always authored data, played through a small call: `restart` or `emit_particle`, `Emit(EmitParams, n)`, `SendEvent` with a position, `SpawnSystemAtLocation` (pooled), `once` and `reset`, `particlefx.play`.
- **Where they differ:** CPU (Shuriken), GPU (VFX Graph, hanabi) or both (Godot's two nodes, Niagara per emitter); a vocabulary from Defold's 4 modifiers through Shuriken's 24 modules to Niagara's open graph; collision with real physics colliders (Shuriken, with script messages) or with cheap stand-ins (collider nodes, SDFs, heightfields, the depth buffer); particles emitting lights (Unity, Niagara).
- **FenrirEngine today:** nothing named particle and no camera-facing quad. Instance lists carry a transform and optional linear colour per copy (`packages/FenrirAssets/include/FenrirAssets/InstanceListData.hpp:19-25`), re-uploaded when their revision moves; a transparent list sorts as a whole by its entity, not copy by copy (`packages/FenrirRenderer/include/FenrirRenderer/Components.hpp:45-47`); transparents sort back to front, one draw each (`SceneRenderer.cpp:3836-3902`, ADR-0051); blend modes are `Opaque`, `Masked`, `AlphaBlend` and `Additive` (`MaterialData.hpp:18-28`); no scene depth reaches shaders, so no soft particles.
- **The Cull stage** is documented as open to a user compute pass "such as a particle simulation" (`src/Core/RenderGraph.hpp:25-29`, ADR-0057), but `RenderFeature` admits none from game code or other packages (`src/Core/RenderFeature.hpp:24-27`): a GPU simulation could live only inside the renderer core today.
- **The glossary** already says to avoid *billboard* for a world 2D quad (`GLOSSARY.md`, 2D drawing).
- **Sources:** Epic's Niagara pages could not be fetched directly; those facts come from search extracts of Epic's pages and are marked so. Unity never says Shuriken simulates on the CPU; that is inferred and marked.

What [What is the engine's particle system?](25-what-the-particle-system-is.md) chooses between: where it simulates; which package owns it; instance lists or a new camera-facing path; fixed parameters, modules, a graph or code; how a game triggers it and what C# gets; collision, lighting and light emission; blend modes, per-particle sorting and soft particles; what the first version holds.
