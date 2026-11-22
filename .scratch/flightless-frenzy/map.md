# Flightless Frenzy

Label: wayfinder:map

## Destination

One spec for Flightless Frenzy as a complete, small game built on FenrirEngine, playable from the title screen to a win or a loss, ready for `/to-spec` to collapse into buildable tickets; plus the list of engine-gap issues under `.scratch/engine-gaps/` that the game needs the engine to close. The first playable slice is the spec's first tickets, not a spec of its own.

## Notes

- **The game design document is the source of truth for what the game is meant to be** (the user, 2026-11-19). The 2022 code was rushed and did not complete it: use "What did the old game ship?" for history and for mistakes to avoid, never as the reference for a rule or a number. Where the decisions settled while charting trim the design document, they stand.
- **The old repo is the source of the art** (the user, 2026-11-19): every game asset in it was made by Darcy Wilson and comes across, credited, with the original Kiki model mandatory and the old level as the game's level. See "Where do the assets come from?".
- **Sources:** the 2022 game design document, the user guide and the Project Vakol engine document (PDFs outside the repo), and the old code at <https://github.com/Wolfleader101/Project_Vakol>. "What did the old game ship?" holds what they say.
- **The core as described in 2022:** a timed 3D platformer. Free every caged friend by walking into the cage, collect coins for score, reach the exit before the timer runs out. Three power-ups: speed boost, slowed timer, double jump. The code delivered less than this, and in third person: see "What did the old game ship?".
- **No stand-ins for missing renderer features** (the user, 2026-11-19): the game does not fake outlines, fog, particles or per-entity colour. Each is an engine issue that must be resolved before the game is complete; the first playable slice goes without.
- **Engine gaps are raised, not worked around.** Anything the game needs that FenrirEngine lacks becomes its own issue under `.scratch/engine-gaps/issues/`, `Status: needs-triage`, linked from the map ticket that found it, saying whether it blocks the game or can be stubbed. It is not built inside the game.
- **The engine/game line:** the engine provides building blocks; what the world is belongs to the game.
- **Use what the engine offers:** renderer and lighting, physics, animation, FenrirUI and Razor panels, C# scripting. The game does not need ragdolls: the design document has none, and they are not yet built. "What does FenrirEngine give a game today?" is the inventory decisions rest on.
- **"How are levels authored and loaded?" waits** (the user, 2026-11-19): the engine's levels and scenes are being designed in another session. Take that ticket only once that design is settled. "The first playable slice and build order" waits with it.
- **Settled while charting** (2026-11-19; the tickets below build on these and do not reopen them):
  - The reimagining starts from the design document's vision, trimmed, with the shipped loop as its spine: Kiki the Kiwi, a virtual New Zealand, friends caged by poachers.
  - **Camera:** third person, with Kiki on screen, as the working intent; "How does Kiki move?" compares it against first person before it is locked.
  - **Enemies** are simple hazards: one or two kinds, defeated by a head stomp. A hit costs time, not health; the timer is the only resource.
  - **Power-ups** are four timed pickups: speed, slowed timer, extra jump, glide.
  - **One finished level**, about five minutes, open and vertical, built so that a second level is new data and not new code.
  - **The look** is stylised and toon-shaded, as the design document wanted.
  - **The game lives in `games/FlightlessFrenzy/`** in this repo, apart from `examples/`, and is treated as its own repo: it leans on no demo code.
- Skills: grilling tickets call `grilling` and `domain-modeling`; prototype tickets call `prototype` and are used where feel is the question (movement, the double jump, the camera). Research tickets call `research` and put their findings on a `research/<name>` branch.
- Plan, don't do: tickets produce decisions, not the game.

## Decisions so far

<!-- one line per resolved ticket: [title](issues/NN-slug.md): gist -->

- [What does FenrirEngine give a game today?](issues/01-what-fenrirengine-gives-a-game.md): the engine covers running, input, rendering with a game's own material shaders, physics with a character body, animation, UI and C# scripting; it lacks a way to build against it from outside, any level or save format, time scale and pause, particles, outlines, animation state machines, navigation, an input action layer, and scene switching and parenting from C#; ragdolls are specified but unbuilt.
- [What did the old game ship?](issues/02-what-the-old-game-shipped.md): a third-person game of about 1,080 lines of C++ with one hand-placed level; cages freed by proximity, a frame-counting timer, three lives; no real jump, power-ups that only score, uncollectable coins, no enemies, menus or animation; a three-band toon shader; assets carry no licence or stated origin.
- [Raise the engine gaps the inventory found](issues/14-raise-the-engine-gaps-the-inventory-found.md): eleven issues in `.scratch/engine-gaps/issues/` beside audio; building against the engine from outside `examples/` and pause block part of the game, C# reach blocks part of a C# game, the rest can be stubbed; scene files are already being planned on the unmerged `fenrir-serialisation` map.
- [How does the game build against the engine?](issues/03-how-the-game-builds-against-the-engine.md): a CMake project of its own with its own presets, adding the engine by path as it would a submodule, examples off; only package targets and engine CMake functions; its own start-up and solution; unit tests but no smoke run, no CI for now; the script-build and asset-copy helpers move into the engine first.
- [C++, C#, or a split?](issues/04-cpp-csharp-or-a-split.md): gameplay and UI in C# with Razor panels; a C++ host that only starts the app, installs packages and registers shaders; the engine binds parenting and scenes for C# before the game needs them; run state on one entity until C# can define resources; rules as plain C# types under `dotnet test`.
- [What is the game loop?](issues/06-the-game-loop.md): title, playing, paused, won, lost; the timer starts on first input and running out is the only loss; enemy hits and falls cost seconds, nothing adds time; cages open on touch and are the checkpoints; the exit opens after the last friend; score from friends, coins, stomps and time left, no rank; best score and time kept in a file the game writes itself.
- [What are the HUD and the menus?](issues/13-hud-and-menus.md): HUD of timer, friends, score and active power-up; a marker to the exit once it opens, none for cages; title, pause, won, lost and options screens as Razor panels; options are camera sensitivity and invert-Y; menus by mouse, Tab and gamepad, Escape to pause and go back; the level frozen and blurred behind a menu.
- [Where do the assets come from?](issues/11-where-assets-come-from.md): all of the 2022 art, made by Darcy Wilson, comes across under CC0 with credit, the old level included; the original Kiki is mandatory, rigid and moved in code at first, with a ticket to try rigging it; friends are Kiki recoloured; anything else is CC0, CC-BY or OFL; GLB only, converted once, no OBJ kept; plain files in the repo under a 50 MB budget.
- [What does the game look like?](issues/12-the-look.md): one toon material shader of the game's own with banded light, a hard cool sun shadow and a rim light; Darcy Wilson's skybox at midday; sun shadows, MSAA, PBR Neutral and a little bloom, the lens effects off; outlines, fog, particles and per-entity colour come from the engine and block the game's completion, with no stand-ins; the numbers go to a prototype.
- [How does Kiki move?](issues/05-how-kiki-moves.md): third person, locked; the grey box's defaults kept: run 7 m/s with no sprint; a held jump rises 2.0 m in 0.38 s, falls at 1.8 × gravity, and a tap rises 0.7 m; 0.12 s each of coyote time and jump buffer; the extra jump rises 1.5 m, the glide sinks at 2.5 m/s, the boost is × 1.5; a ledge of 1.5 m and a gap of about 5 m without power-ups (worked out, not measured); no `CharacterBody` gap; the prototype is on `prototype/flightless-frenzy-movement`.
- [What does the player play with?](issues/07-controls-and-input-devices.md): keyboard with mouse and one gamepad, both live; run, look, one jump button for jump, extra jump and glide, and pause; no rebinding; devices merged in one tested C# type; separate mouse and stick look sliders and one invert-Y; gamepad play waits for the engine's dead zones; rumble raised as a gap; prompts as text for the device used last.
- [How do the power-ups work?](issues/08-power-ups-and-glide.md): speed 1.5 ×, timer at half speed, one extra jump, glide; 15 seconds each to start; kinds run together, the same kind refills; a pickup returns 5 seconds after its effect ends; nothing ends one early; a bar per kind and Kiki's rim colour, blinking for the last 3 seconds; a new feather model for the glide.
- [What are the enemies?](issues/09-enemies.md): a stoat that patrols, chases in a straight line slower than Kiki and is defeated by a stomp, and a poacher by a cage who cannot be defeated, only dazed; a hit or a fall costs 5 seconds; a stomp bounces Kiki 1.2 to 2.0 m and scores 200; stomped stoats stay gone; no path-finding.

## Not yet specified

- **The level itself:** it starts from the 2022 level's geometry; still open are how that is broken up, its landmarks, where the cages, coins, power-ups, enemies and exit sit, and its time limit. Waits on the movement numbers, the enemies, the power-ups and how levels are authored.
- **What freed friends do:** run off, gather at the exit, follow Kiki;.
- **A live title screen:** the level flown over behind the title menu, in place of a plain backdrop.
- **How the story is told:** an opening, an ending, or nothing beyond the title screen.
- **Weather, day and night, and particles** beyond what "What does the game look like?" decides.
- **Difficulty:** one setting or several.
- **CI for the game:** none for now.
- **The game's vocabulary:** whether its terms (friend, cage, coin, power-up, exit) go in `GLOSSARY.md` or a glossary of the game's own.
- **How the game is packaged** to hand to someone else to play.

## Out of scope

- **Audio.** FenrirEngine has none yet; raised as [The engine has no audio](../engine-gaps/issues/01-audio.md), and the game is planned without sound.
- **Combat and health:** the beak peck, the seed spit, damage multipliers, a health bar. Enemies are stomped or avoided.
- **Ability cards** and choosing between abilities; power-ups are timed pickups.
- **More levels and biomes** than the one in the spec.
- **A world editor.**
- **Shay's World, the launcher back to it, and every other university requirement** of the 2022 project.
