# Flightless Frenzy

Label: wayfinder:map

## Destination

A spec for Flightless Frenzy as a game built on FenrirEngine, ready for `/to-spec` to collapse into buildable tickets, plus the list of engine-gap issues under `.scratch/engine-gaps/` that the game needs the engine to close.

*Draft: stated in the charting prompt, confirmed or redrawn by the first grilling round.*

## Notes

- **Sources are inspiration, not a spec:** the 2022 game design document, the user guide and the Project Vakol engine document (PDFs outside the repo), and the old code at <https://github.com/Wolfleader101/Project_Vakol>. "What did the old game ship?" holds what they say.
- **The core as shipped:** a timed first-person 3D platformer. Free every caged friend by walking into the cage, collect coins for score, reach the exit before the timer runs out. Three power-ups: speed boost, slowed timer, double jump.
- **Engine gaps are raised, not worked around.** Anything the game needs that FenrirEngine lacks becomes its own issue under `.scratch/engine-gaps/issues/`, `Status: needs-triage`, linked from the map ticket that found it, saying whether it blocks the game or can be stubbed. It is not built inside the game.
- **The engine/game line:** the engine provides building blocks; what the world is belongs to the game.
- **Use what the engine offers:** renderer and lighting, physics and ragdolls, animation, FenrirUI and Razor panels, C# scripting. "What does FenrirEngine give a game today?" is the inventory decisions rest on.
- Skills: grilling tickets call `grilling` and `domain-modeling`; prototype tickets call `prototype` and are used where feel is the question (movement, the double jump, the camera). Research tickets call `research` and put their findings on a `research/<name>` branch.
- Plan, don't do: tickets produce decisions, not the game.

## Decisions so far

<!-- one line per resolved ticket: [title](issues/NN-slug.md): gist -->

## Not yet specified

<!-- filled in once the first grilling rounds have mapped the frontier -->

## Out of scope

- **Audio.** FenrirEngine has none yet; recorded as an engine gap, and the game is planned without sound.
- **Shay's World, the launcher back to it, and every other university requirement** of the 2022 project.
