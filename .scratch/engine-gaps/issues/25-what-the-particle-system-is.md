# What is the engine's particle system?

Type: grilling
Status: open
Blocked by: 24
Settles: [No particles or billboards](06-particles-and-billboards.md)

## Question

Flightless Frenzy wants bursts when a coin is collected, a cage opens, a power-up starts and ends, an enemy is stomped and Kiki lands ([What does the game look like?](../../flightless-frenzy/issues/12-the-look.md)), from the engine and not from instanced meshes the game moves itself; weather later. With [How do engines split particles between the engine and the game?](24-how-engines-split-particles.md) in hand:

- **The engine/game line:** what an effect is (an emitter component, a data asset, a function call that spawns a burst), and which parts are the engine's block and which the game's choice.
- **The first version:** CPU or GPU simulation; billboards only, or meshes too; trails and decals in or out; collision in or out.
- **Rendering:** a pass of its own or the transparent pass; sorting; lit or unlit; soft particles (which need scene depth, shared with [How does a game draw outlines and add its own render pass?](23-how-a-game-draws-outlines-and-adds-a-pass.md)).
- **Triggering from C#:** a one-shot burst at a position or attached to an entity.
- **Authoring:** code first, or a data asset that waits on the `fenrir-serialisation` map.
- **Where it is planned:** this map, or the renderer map's "Particles" line graduated into a spec there.

Outcome: a spec-ready design.
