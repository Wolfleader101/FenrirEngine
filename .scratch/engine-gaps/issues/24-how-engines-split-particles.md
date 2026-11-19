# How do engines split particles between the engine and the game?

Type: research
Status: open
Blocked by: none
Settles: [No particles or billboards](06-particles-and-billboards.md)

## Question

What do Godot 4, Unity (Shuriken, VFX Graph), Unreal (Niagara), Bevy (`bevy_hanabi`) and one lighter engine offer for particles: the data model, CPU or GPU simulation, one-shot bursts triggered from game code, billboard, mesh and trail rendering, sorting and blending with transparents, lighting, collision, and authoring? What did each ship first? What does FenrirRenderer offer today (instance lists with per-copy colour, the transparent pass, blend modes)?

Findings go in `.scratch/engine-gaps/research/engine-particles.md` on the `research/engine-particles` branch.
