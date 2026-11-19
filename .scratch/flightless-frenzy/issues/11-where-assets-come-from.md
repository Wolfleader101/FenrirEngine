# Where do the assets come from?

Type: grilling
Status: resolved
Blocked by: 01, 02

## Question

Where do Kiki, the friends, the cages, coins, power-ups, enemies, the level's pieces, the animations, textures and fonts come from? The old repo's assets where their origin and licence allow it, freely licensed packs, new work, or placeholders for the first build with a named plan to replace them.

Decide per kind of asset, and decide the rules: which licences are acceptable, where attribution is recorded, which formats the game uses given what the engine loads, and whether binary assets sit in this repo. A skinned, animated kiwi is the hardest piece; say what the game does if there isn't one.

## Answer

Grilled 2026-11-19, in two rounds. **The game's art is the 2022 game's art, all of it made by Darcy Wilson, brought across and credited.**

This is the one place the old repo is a source and not only history: the design document stays the source of truth for rules, and the old repo is the source of the art.

### The old assets

- **Who made them** (the user, 2026-11-19): Darcy Wilson made every game asset in the old repo, Kiki and the level textures included. The old repo itself states no origin or licence, so this rests on the user's word.
- **Licence and credit:** CC0, credited in `games/FlightlessFrenzy/assets/ATTRIBUTION.md` with the wording and link of `examples/assets/skybox-darcy/attribution.txt` ("Created by Darcy Wilson (https://darcysdungeon.nekoweb.org/)"), naming the game's assets. That file's own text covers only the skybox; the user confirmed the same terms for these.
- **Everything comes across:** from the old repo's `assets/Models`, `assets/Materials` and `assets/Textures`:
  - Kiki (`kiki.obj`, body and eyes textures);
  - the cage, the three coins, the three power-up models and the exit platform;
  - the level: `levelGeo.obj`, `cliffTrees.obj`, `wallGrass.obj` and the level textures.

  Left behind: Shay's World (`assets/data/`), and the test pieces not in the old scene (`cube`, `sphere`, `phil`, `plane`, `portal`, `stairs`).
- **The old level is the game's level.** It may need breaking into pieces; how is for "How are levels authored and loaded?". New geometry is made only where the game needs something the old level lacks.

### Kiki

- **The original Kiki model is mandatory.** No stand-in character, and no search for another kiwi.
- **It has no skeleton** (a static mesh of 335 vertices, never animated in 2022). The first build uses it as one rigid model brought to life in code: squash and stretch on jump and landing, a lean into turns, a bob while running, a tilt while gliding.
- **Rigging it is to be tried.** The spec carries a ticket to give the same mesh a skeleton and clips in Blender (idle, run, jump, fall, glide, land, a stomp bounce) and play them through the engine's animation system. Rigging changes no vertices, so the model stays the original. If the rig does not work out, the code-driven motion stays.
- **A caged friend** is the Kiki model with a different body colour, and whatever motion Kiki has.

### Everything else

- **Other assets stay free to share:** CC0 or CC-BY, and the SIL Open Font Licence for fonts. Nothing non-commercial, share-alike or of unknown origin. Each one is listed in `ATTRIBUTION.md` with its source, author, licence and any changes.
- **Enemies** have no old models. They are found under those licences or modelled simply, once "What are the enemies?" says which kinds exist.
- **The display font** is Fredoka (SIL Open Font Licence) as a starting choice; "What does the game look like?" may change it.

### Rules

- **Formats:** GLB for models with their textures inside, PNG for loose images, TTF or OTF for fonts. No FBX or OBJ in the game.
- **Conversion:** the old OBJ models are converted once to GLB in Blender. The OBJ files are not kept in this repo; the old repo remains their source. One file to mend on the way: `coinBronze_diffuse.jpg` holds PNG data.
- **Where files sit:** committed as plain files under `games/FlightlessFrenzy/assets/`, no Git LFS. Blender files made for the game (the rig, the broken-up level) sit in `games/FlightlessFrenzy/art/` and are not copied to the build.
- **Budget:** about 5 MB a file and 50 MB in total. Outgrowing it is the moment to move the game to its own repo.

No engine gap came out of this ticket.
