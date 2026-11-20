# What does the toon look need?

Type: prototype
Status: open
Blocked by: 11, 12

## Question

"What does the game look like?" fixed the intent: one toon material shader, banded light, a hard cool-tinted sun shadow, a rim light, Darcy Wilson's skybox at midday, PBR Neutral tonemapping. What are its numbers, and do the 2022 assets carry it?

Build a throwaway scene: the original Kiki model and a piece of the 2022 level, converted to GLB, under a first version of the toon shader with the settings from that ticket.

Settle by looking at it:

- **Bands:** how many, and where the steps fall.
- **Shadow:** its tint and how dark the darkest band is; whether cascaded sun shadows give a clean hard edge at this level's size.
- **Rim light:** its width and strength.
- **The textures:** whether the 64 and 128 pixel textures hold up under banded light, with which filtering, or need redrawing.
- **The sky's fill light:** the neutral colour that keeps the skybox from tinting the scene.
- **Bloom:** how little is enough for coins and power-ups.

Outlines, fog, particles and per-entity colour are out of this prototype: they wait on the engine and the game builds no stand-in.

The answer records the numbers; the prototype stays on a `prototype/` branch as a pointer. Anything a game's material shader cannot reach becomes an engine gap.
