# Flightless Frenzy

Label: wayfinder:map

## Destination

One spec for Flightless Frenzy as a complete, small game built on FenrirEngine, playable from the title screen to a win or a loss, ready for `/to-spec` to collapse into buildable tickets; plus the list of engine-gap issues under `.scratch/engine-gaps/` that the game needs the engine to close. The first playable slice is the spec's first tickets, not a spec of its own.

## Notes

- **Sources are inspiration, not a spec:** the 2022 game design document, the user guide and the Project Vakol engine document (PDFs outside the repo), and the old code at <https://github.com/Wolfleader101/Project_Vakol>. "What did the old game ship?" holds what they say.
- **The core as described in 2022:** a timed 3D platformer. Free every caged friend by walking into the cage, collect coins for score, reach the exit before the timer runs out. Three power-ups: speed boost, slowed timer, double jump. The code delivered less than this, and in third person: see "What did the old game ship?".
- **Engine gaps are raised, not worked around.** Anything the game needs that FenrirEngine lacks becomes its own issue under `.scratch/engine-gaps/issues/`, `Status: needs-triage`, linked from the map ticket that found it, saying whether it blocks the game or can be stubbed. It is not built inside the game.
- **The engine/game line:** the engine provides building blocks; what the world is belongs to the game.
- **Use what the engine offers:** renderer and lighting, physics and ragdolls, animation, FenrirUI and Razor panels, C# scripting. "What does FenrirEngine give a game today?" is the inventory decisions rest on.
- **Settled while charting** (2026-11-19; the tickets below build on these and do not reopen them):
  - The reimagining starts from the design document's vision, trimmed, with the shipped loop as its spine: Kiki the Kiwi, a virtual New Zealand, friends caged by poachers.
  - **Camera:** third person, with Kiki on screen, as the working intent; "How does Kiki move?" compares it against first person before it is locked.
  - **Enemies** are simple hazards: one or two kinds, defeated by a head stomp. A hit costs time, not health; the timer is the only resource.
  - **Power-ups** are four timed pickups: speed, slowed timer, extra jump, glide.
  - **One finished level**, about five minutes, open and vertical, built so that a second level is new data and not new code.
  - **The look** is stylised and toon-shaded, as the design document wanted.
  - **The game lives in `games/FlightlessFrenzy/`** in this repo, apart from `examples/`, built against the engine as an outside program would be.
- Skills: grilling tickets call `grilling` and `domain-modeling`; prototype tickets call `prototype` and are used where feel is the question (movement, the double jump, the camera). Research tickets call `research` and put their findings on a `research/<name>` branch.
- Plan, don't do: tickets produce decisions, not the game.

## Decisions so far

<!-- one line per resolved ticket: [title](issues/NN-slug.md): gist -->

- [What did the old game ship?](issues/02-what-the-old-game-shipped.md): a third-person game of about 1,080 lines of C++ with one hand-placed level; cages freed by proximity, a frame-counting timer, three lives; no real jump, power-ups that only score, uncollectable coins, no enemies, menus or animation; a three-band toon shader; assets carry no licence or stated origin.

## Not yet specified

- **The level itself:** its layout, landmarks, where the cages, coins, power-ups, enemies and exit sit, and its time limit. Waits on the movement numbers, the enemies, the power-ups and how levels are authored.
- **What freed friends do:** run off, gather at the exit, follow Kiki; and where else the game shows off physics (ragdolls, things to knock over).
- **How the story is told:** an opening, an ending, or nothing beyond the title screen.
- **Weather, day and night, and particles** beyond what "What does the game look like?" decides.
- **Difficulty:** one setting or several.
- **How game code is tested**, once the language is chosen.
- **The game's vocabulary:** whether its terms (friend, cage, coin, power-up, exit) go in `GLOSSARY.md` or a glossary of the game's own.
- **How the game is packaged** to hand to someone else to play.

## Out of scope

- **Audio.** FenrirEngine has none yet; raised as [The engine has no audio](../engine-gaps/issues/01-audio.md), and the game is planned without sound.
- **Combat and health:** the beak peck, the seed spit, damage multipliers, a health bar. Enemies are stomped or avoided.
- **Ability cards** and choosing between abilities; power-ups are timed pickups.
- **More levels and biomes** than the one in the spec.
- **A world editor.**
- **Shay's World, the launcher back to it, and every other university requirement** of the 2022 project.
