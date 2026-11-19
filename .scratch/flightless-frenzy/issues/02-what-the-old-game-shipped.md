# What did the old game ship?

Type: research
Status: resolved
Blocked by: none

## Question

What was Flightless Frenzy as actually built in 2022, as opposed to what its design document planned? Answer from the old repository, <https://github.com/Wolfleader101/Project_Vakol> (`main`), and the Project Vakol engine document (`C:\Users\james\Downloads\Project_Vakol_Doc.pdf`), read beside the game design document and user guide in the same folder.

- **The game's code:** where the game lives in the repo as distinct from the engine (scripts, scenes, level files), in what language, and how it is structured.
- **The player:** movement speeds, jump height and gravity, air control, camera (first or third person, limits), collision shape; the numbers where the code gives them.
- **The loop:** timer length and behaviour, cages and how freeing works, coins and scoring, the exit and its condition, win and lose handling, restart.
- **Power-ups:** speed, slowed timer and double jump: values, durations, how they are picked up and shown.
- **Planned but not shipped:** for each design-document feature (enemies and their AI, health, melee and seed spit, ability cards, glide, sprint, several biomes, weather, day and night, cel shading, menus, a world editor), whether it exists in the code, partly or not at all.
- **Levels:** how many, how they are authored and stored, their layout and size, where the cages, coins, power-ups and exit sit.
- **Assets:** the models, textures, animations, fonts and sounds in the repo, their formats, and what licence or origin each has where the repo says (the repo's own licence included), so a later ticket can decide what may be reused.
- **HUD and menus:** what was on screen.
- **Known problems:** anything the code, commit messages or documents admit was rough.

Leave Shay's World and the other university deliverables out except where they explain a game behaviour. Give facts with file pointers, not recommendations.

## Answer

Full findings, with file pointers into the old repo: `.scratch/flightless-frenzy/research/old-game.md` on branch `research/flightless-frenzy-old-game` (commit f7089fab; never merged). Read from old-repo `main` at 207616e (2022-11-04); nothing was built or run. The Project Vakol document is a six-page planning document, and only its text could be extracted.

**Two things the charting prompt had wrong.** The old game is third person (an orbit camera throughout), not first person. And the user guide describes more than the code does: the three power-ups only add score, and coins cannot be collected.

- **Code:** about 1,080 lines of C++17 in `Game/`, compiled into the engine's exe. One `Game` class holds every rule in its update. No scripting. One scene, `assets/json/scene.json`, 32 entities placed by hand.
- **Player:** `kiki.obj`, a capsule (radius 1.98, height 2.70) on a dynamic body, moved by teleporting 20 units/s each frame. No real jump: holding Space rises at 20 units/s with no grounded check. No acceleration, sprint or glide.
- **Camera:** third-person orbit, distance 25, pitch ±65°, full yaw, field of view 45°, no camera collision.
- **Loop:** the timer counts frames, not seconds (about 328 s at 60 fps, about 138 s at 144). Three lives, in no document: falling below y = -50 respawns at the start and costs one. A cage is freed by coming within 8 units of it, for 1000 points; there is no friend model. The exit is a fixed box, checked once no friends remain. Win and lose show an image; there is no restart, and the win score overflows.
- **Coins:** bronze 300, silver 500, gold 700 in code, but the pickup test never matches them; 15 are placed.
- **Power-ups:** one each of double jump, slow timer and speed; each gives 100 points and nothing else.
- **Level:** one, modelled in Maya as three OBJ files, about 660 by 680 units in plan with a 315-unit height range; an upper area, a lower shelf and the exit platform. Nine cages at seven positions. The planned one unit to a metre was not held.
- **Shipped from the design document:** the third-person camera, the timed free-friends-and-exit loop, a three-band toon shader (no outlines or shadows), a skybox, a JSON scene. **Partly:** coins, jump, HUD (four text lines), debug draw. **Absent:** working power-ups, sprint, glide, enemies, health, melee and seed spit, ability cards, more levels or biomes, weather, day and night, menus, audio, animation, a world editor, scripting.
- **Assets:** the repo has no licence, README or credits and states no origin for any asset; the design document says all rights reserved, Team Vakol, and there are four committers. Models are small OBJ files (Kiki is 335 vertices); 40 small diffuse textures that look like a numbered pack of unstated source; no animations, no fonts.
- **HUD:** one Dear ImGui window with friends, lives, score and time as bare numbers. No menus, pause or title screen.
- **Known problems, admitted in the code:** no grounded check, power-ups and refactoring skipped for lack of time; all game logic was written in the last three days.
