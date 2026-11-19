# What the old game shipped

Research for [What did the old game ship?](../issues/02-what-the-old-game-shipped.md). Facts only.

## Sources and how to read the pointers

- **Old repo:** <https://github.com/Wolfleader101/Project_Vakol>, branch `main` at `207616e` (merge of `dev`, 2022-11-04 23:31 +0800). 457 commits, 2022-07-08 to 2022-11-04. Every `path:line` below is into that commit unless a hash is given. Read only; nothing was built or run, so every behaviour below is a reading of the code, not an observation of the running game.
- **Other branches:** `dev` (`c164f47`) is an ancestor of `main`. `redesign` (`5ab17b7`, 2022-11-18, three commits) deletes the whole `Game/` folder, the Shay's World submodule and the GUI helpers; it holds no later version of the game.
- **GDD:** `Game Design Document.docx.pdf`, version 1.0, dated 2022-08-25 (history entry 2022-09-07). Page numbers are the printed ones.
- **User guide:** `User Guide.pdf`, one page.
- **Engine document:** `Project_Vakol_Doc.pdf`. No PDF library is installed on the machine (pypdf, PyPDF2, pdfminer, fitz and pdfplumber all missing) and the Read tool cannot page it, so its text was pulled with a small standard-library script (inflate the streams, decode through the fonts' ToUnicode maps). It came out as 6 pages of text; the last page breaks off mid-sentence ("PhysEntity - an object"), and images were not read. It is a **planning** document (title "Project Vakol Planning"), not a description of the finished engine.
- **A caveat on "shipped":** `src/main.cpp:15-20` has the Shay's World loop commented out with the note "uncomment when we make submission build". The build handed in may have differed from `main` by at least that. The repo has no tag, release or binary.

### The premise about first and third person

The user guide does not say first person; it says only "Move the mouse to control your camera". The code is third person throughout (`Vakol::ThirdPersonCamera`, `Game/Game.hpp:111`). The only "first-person" wording in any source is one line of the GDD (p.10, Camera overview: "first-person perspective, until the end of the level"), contradicted by the three bullets directly under it and by GDD p.6, p.8 and the engine document p.1 and p.4, which all say third person.

## The game's code

- **Where:** `Game/` at the repo root, beside the engine in `src/`. C++17, compiled into the same single executable as the engine (`CMakeLists.txt:15-22`, `Game/CmakeLists.txt:1-23`). No scripting language exists in the repo; the GDD's "Lua or C#" (p.11) and the engine document's JavaScript (p.1) were not started.
- **Case clash:** git tracks two spellings, `Game/` (Game, Inventory, Items, Player, Trigger) and `game/` (Input, UI), plus both `Game/CmakeLists.txt` and `game/CmakeLists.txt`. It only resolves on a case-insensitive file system. `CMakeLists.txt:19` adds `game`.
- **Size:** about 1,080 lines including doc comments. Files: `Game/Game.{hpp,cpp}` (129 + 277), `Game/Player/Player.{hpp,cpp}` (70 + 44), `Game/Input/Input.{hpp,cpp}` (75 + 102), `Game/Trigger/Trigger.{h,cpp}` (159 + 107), `Game/Inventory/Inventory.{hpp,cpp}` (37 + 11), `Game/Items/Item.hpp` (48), `Game/UI/UI.{hpp,cpp}` (1 + 0).
- **Structure:** one class `Game : Vakol::Layer` (`Game/Game.hpp:15`) pushed onto the application's layer stack (`src/main.cpp:39`). All rules live in `Game::OnUpdate` (`Game/Game.cpp:70-219`). `Player` only moves the body (`Game/Player/Player.cpp:20-44`). `Input` is a static class turning key events into an axis vector (`Game/Input/Input.cpp:18-49`). `Trigger::TriggerZone` is an axis-aligned box with a point test, written for Shay's World and reused for the exit (`Game/Trigger/Trigger.cpp:49-65`, comment at line 49).
- **Dead code:** `Inventory`, `Item`, `Collectable`, `Powerup` (`Game/Items/Item.hpp:10-48`) are never instantiated. `UI.cpp` is empty. `PlayerState` (IDLE, WALKING, RUNNING, JUMPING, FALLING, DEAD; `Game/Player/Player.hpp:10-18`) is never set or read. `Input::GetKey` compares against `KeyPressed`, which is never written (`Game/Input/Input.cpp:14,78-84`).
- **Scene:** one file, `assets/json/scene.json` (1,510 lines, 32 entities), loaded by a hard-coded path (`src/Application/Application.cpp:19`). `assets/json/EngineDestructor.json` is a stale two-entity serialisation test that references a `world.obj` not in the repo.
- **Engine under it:** OpenGL 4.6 compatibility profile through GLFW and glad (`src/Window/Window.cpp:13-15`), entt, ReactPhysics3D (submodule pinned at `17dd22e`), Dear ImGui docking branch, nlohmann json, spdlog, glm, stb_image; a hand-written OBJ and MTL loader (`src/Resources/Model/Model.cpp`, `src/Resources/Material/Material.cpp`). Window "Project Vakol", 1280 x 720 (`src/Application/Application.hpp:35`), cursor captured (`src/Window/Window.cpp:33`).

## The player

- **Entity:** tag `player`, type PLAYER, model `kiki.obj`, scale 0.1, start position (0, 20, 0) (`assets/json/scene.json`, the first of the two entities ending `"id": 6`, around lines 331-377). At that scale the model is about 4.0 wide, 5.4 tall, 8.6 long (OBJ bounds x +-19.83, y 0 to 53.9, z -56.14 to 30.16).
- **Body:** dynamic (`"Type": 2`), mass 2, gravity on, linear damping 0, angular damping 2, angular lock (0, 1, 0). World gravity is never set, so it is ReactPhysics3D's default (0, -9.81, 0) (`src/Physics/Physics.cpp:16`). Fixed step 1/60 s with an accumulator and interpolated transforms (`src/Physics/Physics.hpp:210`, `src/Physics/Physics.cpp:129-168`).
- **Collision shape:** capsule (`"Collider Shape": 2`), radius = half model width x scale = 1.98, cylinder height = half model height x scale = 2.70, offset (0, 3, 0) (`src/Physics/Physics.cpp:94-104`, `src/ECS/Components.cpp:52`).
- **Movement:** `m_speed = 20.0` units per second, the only movement number in use (`Game/Player/Player.cpp:16`). Each frame the body is teleported: `newPos = pos + (forward * z + up * y + right * x) * m_speed * deltaTime`, then `setTransform` (`Game/Player/Player.cpp:38-43`). No force, impulse or velocity is ever applied by the game. No acceleration, friction or diagonal normalisation (W and D together move at about 28.3).
- **Jump:** there is none in the platformer sense. Space sets `Axis.y = 1` while held (`Game/Input/Input.cpp:26-27,42-43`), which raises the body along the camera's up vector at the same 20 units per second for as long as it is held, against gravity. The comment reads "no check for grounded due to lack of time" (`Game/Player/Player.cpp:37`); the commit is "add jump (no grounded check)" (`ea1072f`, 2022-11-04 20:04). `m_jumpHeight = 10.0` is set and never read (`Game/Player/Player.cpp:15`).
- **Air control:** identical to ground control, since nothing knows whether the player is grounded.
- **Unused stats:** `m_health = 100`, `m_damage = 10` (`Game/Player/Player.cpp:14,17`); never read.
- **Facing:** the body's rotation is set from the camera yaw every frame (`Game/Player/Player.cpp:33,43`; commit `48d9e35` "kiki now faces the camera direction").
- **Camera:** third person orbit, default-constructed in the renderer (`src/Renderer/Renderer.hpp:67`). Distance 25, pitch clamp -65 to +65 degrees, full 360 degrees of yaw, smoothing speed 5 (`src/Resources/Camera/ThirdPersonCamera.cpp:12-30,118-123`). The game sets a focal-point height offset of 7.5, sensitivity 0.25 on both axes, X not inverted, Y inverted (`Game/Game.cpp:56-62`). No zoom (scroll is ignored, `Game/Input/Input.cpp:73-76`), no collision with the level, no first-person mode. Projection: 45 degree field of view, near 0.1, far 1000 (`src/Renderer/Renderer.cpp:144`).
- **Controls bound:** W, A, S, D, Space, mouse look, Escape to quit (`Game/Input/Input.cpp:18-33`, `src/Application/Application.cpp:149-155`). Mouse buttons only log "LEFT!", "RIGHT!", "MIDDLE!" (`Game/Input/Input.cpp:86-103`). Shift is not bound.

## The loop

- **Timer:** `time_remaining` is an `int` set to 20000; the HUD shows `time_remaining / 100`, so it starts at 200 (`Game/Game.hpp:118-119`, `Game/Game.cpp:22-23,81`). It is decremented with `time_remaining -= time.deltaTime` (`Game/Game.cpp:80`), where `deltaTime` is seconds as a `double` (`src/Time/Time.cpp:8-12`). Subtracting a fraction from an int and truncating loses exactly 1 per frame, so the timer counts **frames, not seconds**: the display drops one unit every 100 frames.
- **Time-out test:** `time_remaining - time.curTime < 0`, with `curTime` being seconds since GLFW started (`Game/Game.cpp:74`). At a steady frame rate f that fires after about 20000 / (f + 1) seconds: about 328 s (5 min 28 s) at 60 fps, about 138 s at 144 fps, with the display reading about 3 rather than 0. No swap interval is set anywhere, so the frame rate is whatever the driver gives. The GDD planned 5 to 10 minutes per level (p.19).
- **Lives:** 3 (`Game/Game.cpp:25`). Falling below y = -50 teleports the player to (0, 10, 0), clears forces and takes a life (`Game/Game.cpp:110-122`). Zero lives ends the game as a loss (`Game/Game.cpp:74`). Lives are in neither design document nor the user guide.
- **Cages:** entities typed FRIEND are counted at start (`Game/Game.cpp:37-43`). A cage is freed when the player's position is within 8 units of the cage's on each axis separately (a 16-unit cube, not a physics contact): the entity is removed, the count drops, score +1000, one cage per frame (`Game/Game.cpp:128-141`). Nothing is spawned or animated in its place; there is no friend model.
- **Coins:** the scoring branch exists: bronze +300, silver +500, gold +700 (`Game/Game.cpp:153-164`). It is **unreachable with the committed scene**: the pickup test is `type > 6` (`Game/Game.cpp:144`), and all 15 coin entities carry `"Entity Type": 5` (COLLECTABLE), not 7, 8 or 9 (`assets/json/scene.json`, entities with ids 9 to 23 and 118; enum at `src/ECS/Components.hpp:172`). No commit of the scene file ever held a type 7, 8 or 9 (`git log -S'"Entity Type": 7'` is empty). Coins are static boxes with colliders, so by this reading they stood in the level as solid props.
- **Pickups stop after the last cage:** the whole pickup loop sits inside `if (friends_remaining > 0)` (`Game/Game.cpp:124`), so nothing can be collected once every cage is freed.
- **Exit:** a hard-coded box, x 236 to 305, y -50 to -40, z -263 to -236 (`Game/Game.cpp:46`), around the `exitPlatform.obj` entity at (270, -45, -250) (`assets/json/scene.json:1-48`). It is tested only when `friends_remaining == 0` (`Game/Game.cpp:186-197`). The platform is always present; `m_EndZoneSpawned` is never used (`Game/Game.hpp:128`), and `HEAD_FOR_THE_EXIT.png` is never shown.
- **Win:** `m_win = true`, the ImGui window grows to 1270 x 960 and shows `You_Win.png` (`Game/Game.cpp:202-209`). `m_score *= time_remaining` runs every frame from then on (`Game/Game.cpp:206`), so the score overflows rather than being a one-off time bonus.
- **Lose:** same window with `Bad_Luck.png` (`Game/Game.cpp:210-216`).
- **Restart:** none. After the end the layer does nothing but redraw the image; the player is no longer updated. Escape calls `exit(0)` (`src/Application/Application.cpp:151-153`); closing the window ends the run loop (`src/Application/Application.cpp:136-140`). The user guide's "press the X to go back to Shay's World" depends on the loop that is commented out in `src/main.cpp:15-20`.

## Power-ups

Three entities exist, one of each (`assets/json/scene.json`, ids 28 to 30), typed DJUMP (10), SLOW (11), FAST (12). They are picked up by the same 8-unit proximity test and removed.

| Power-up | Model | Position | On pickup (`Game/Game.cpp:165-179`) |
|---|---|---|---|
| Double jump | `powerupDoubleJump.obj` | (-24, 23, 75.4) | score +100; comment "should be alteration to jump. no time" |
| Slow timer | `powerupSlowDown.obj` | (-119, -1, 58) | score +100; comment "SLOW DOWN TIME" |
| Speed | `powerupSpeedUp.obj` | (-157, -29, -301) | score +100; comment "Speed up kiki" |

No effect, value or duration is implemented for any of them, and nothing is shown on pickup beyond the score changing. The user guide describes all three as working. The speed power-up sits 5 units directly above a cage (id 27 at (-157, -34, -301)).

## Planned but not shipped

GDD page in brackets. "Absent" means no code and no asset.

| Design-document feature | State | Evidence |
|---|---|---|
| Third-person orbit camera with pitch limits (p.10) | Shipped | `src/Resources/Camera/ThirdPersonCamera.cpp` |
| Timed level, free all friends, then reach exit (p.6, p.19) | Shipped, roughly | `Game/Game.cpp:70-198`; timer counts frames |
| Coins for score (p.8, p.9) | Partly | models, textures, entities and scoring code exist; not collectable as committed |
| Cel shading (p.8, p.12) | Shipped | every model uses `toonVS/toonFS` (`src/Resources/Model/Model.cpp:207`); 3 diffuse bands, one hard-coded point light at (0, 100, -100), intensity 0.8 (`assets/Shaders/toonFS.glsl:12-34`, `src/Renderer/Renderer.cpp:166-168`). No outlines, no shadows |
| Skybox (p.10) | Shipped | one cubemap, six 256 x 256 PNGs (`src/Renderer/Renderer.cpp:63-71`) |
| JSON scenes (p.11) | Shipped | `src/JSON/Json.cpp`; no hot reload |
| ImGui HUD (p.15) | Partly | four text lines only |
| Jump (p.16) | Partly | hold Space to rise; no grounded check |
| Double jump, triple jump (p.16, p.17) | Absent | pickup gives score only |
| Timer slowdown (p.17) | Absent | pickup gives score only |
| Speed power-up (user guide) | Absent | pickup gives score only |
| Sprint on Shift (p.9, p.16) | Absent | no binding; `RUNNING` state unused |
| Glide (p.11, p.17) | Absent | |
| Enemies and AI (p.8, p.14) | Absent | only the `ENEMY` enum value (`src/ECS/Components.hpp:172`) |
| Health (p.15, p.16) | Absent | `m_health` set, never read; lives used instead |
| Melee peck, head bop (p.17) | Absent | `m_damage` set, never read; left click logs only |
| Seed spit, crosshair (p.16, p.17) | Absent | |
| Ability cards, select by scroll, use by right click (p.16, p.17) | Absent | scroll and right click do nothing |
| 2x damage, 2x score (p.17) | Absent | |
| Several biomes (p.9) | Absent | one grass and rock level |
| Several levels, open and closed (p.13, p.19) | Absent | one scene, hard-coded path |
| Randomised collectable spawns (p.9) | Absent | |
| Weather, particles (p.10) | Absent | |
| Day and night (p.10) | Absent | one skybox, one fixed light |
| Menus, options, difficulty (p.15, p.19) | Absent | the game starts straight into the level |
| Debug displays (p.15) | Partly | physics debug draw exists, switched off (`src/Application/Application.cpp:60`) |
| End-of-level view of Kiki on the platform (p.10) | Absent | |
| Music and sound through OpenAL (p.18) | Absent | no audio library; one unused `step.wav` |
| Animation | Absent | static OBJ only; no skeletal or keyframe code |
| World editor (p.20) | Absent | scene JSON edited by hand |
| Scripting (p.11) | Absent | |
| Engine document extras: ragdolls, level editor, JavaScript, object pooling, jet boosters, online co-op, cross-platform, Bullet, ImGuizmo (p.1, p.2) | Absent | ReactPhysics3D used in place of Bullet |

## Levels

- **Count:** one.
- **Authoring:** geometry modelled in Maya (the OBJ headers carry Maya's units comment and `lambertNSG` material names) and exported as three OBJ files; placement of everything else typed by hand into `assets/json/scene.json`.
- **Geometry entities** (all at position (0, -1, 0), scale 0.05, static, triangle-mesh colliders; `assets/json/scene.json`, second `"id": 6`, ids 7 and 8):
  - `levelGeo.obj`: 3,708 vertices, 5,531 faces, 134 groups, 781 KB. Scaled bounds about x -327 to 334, y -206 to 109, z -386 to 296: roughly 660 x 680 units in plan.
  - `cliffTrees.obj`: 192 vertices, boundary tree cards.
  - `wallGrass.obj`: 162 vertices.
- **Scale:** the GDD and engine document set 1 unit = 1 metre. Kiki is about 5.4 units tall and moves 20 units per second, so the stated scale was not held.
- **Layout by coordinates:** spawn (0, 20, 0); an upper area with most objects at y between -6 and 23; a lower shelf at y about -35 to -37.5 for z between -255 and -318 (the user guide's "lower cliff platform"); the exit platform beyond it at (270, -45, -250), 64 x 8 x 64 units, a kinematic box.
- **Cages:** 9 entities, so the counter starts at 9, at 7 distinct positions; ids 1, 24 and 26 are stacked at the same point (20, -37.5, -255) and free on three consecutive frames. The others: (30, -1, 165), (-60, 4, -100), (90, 4, 75), (-35, 7, 92), (73.1, 7, -45.5), (-157, -34, -301). Each is `cage.obj` (a textured 8-vertex box, about 5.3 x 4.5 x 5.3 at scale 0.05).
- **Coins:** 15 entities: 1 bronze at (0, 2, 30); 10 silver at (70, 2, 30), (17, 4, -122), (9.8, 4, -127), (-35, -6, -61), (106, 6, -123), (127, 6, -144), (-9, -35, -257), (-34, -35, -311), (-82, -35, -265), (-101, -35, -318); 4 gold at (-5, 4, -123), (-118, -1, -68), (-121, -1, 39), (114, 8, -131).
- **Highest possible score before the end multiplier, as committed:** 9 x 1000 + 3 x 100 = 9,300. With coins working it would be 9,300 + 300 + 5,000 + 2,800 = 17,400.
- **Scene file oddities:** two entities share id 6 (player and world) and one coin has id 118.

## Assets

**Licence:** the repo has no LICENSE, COPYING, NOTICE or README file and no credits file. Nothing in the repo states an origin or licence for any model, texture or sound. The GDD stamps every page "Copyright (C) 2022 Team Vakol - All rights reserved" and says "All work Copyright (c) 2022 by Team Vakol" (p.4); it also says most sounds would come from "free online sources" (p.18), and the engine document says models would be made in Blender or Maya (p.5) and music would be royalty free (p.5). Four people committed (`git shortlog`), so the game's own assets are a four-author work with no stated licence.

**Game models** (`assets/Models/`, Wavefront OBJ, text; materials in `assets/Materials/*.mtl`):

| File | Vertices / faces | Note |
|---|---|---|
| `kiki.obj` | 335 / 628 | player; materials `body`, `eyes`; Maya export |
| `cage.obj` | 8 / 12 | box |
| `coinBronze.obj`, `coinSilver.obj`, `coinGold.obj` | 14 / 24 each | |
| `powerupDoubleJump.obj`, `powerupSlowDown.obj`, `powerupSpeedUp.obj` | 48 / 92 each | |
| `exitPlatform.obj` | 58 / 112 | |
| `levelGeo.obj`, `cliffTrees.obj`, `wallGrass.obj` | see Levels | |
| `cube.obj`, `sphere.obj`, `phil.obj`, `OBJs/plane.obj`, `OBJs/portal.obj`, `OBJs/stairs/*` | | Blender test and Shay's World pieces, not in the scene |

**Game textures** (`assets/Textures/`, 40 files, all tiny):

- Diffuse maps, 64 x 64 or 128 x 128, JPEG or PNG: `kiki_body.jpg`, `kiki_eyes.png`, `cage_diffuse.jpg`, three `coin*_diffuse.jpg` (`coinBronze_diffuse.jpg` is PNG data with a .jpg name), three `powerup*_diffuse.jpg`, `exitPlatform_Diffuse.jpg`, and the level set (`floor_1_GEOShape_color.jpg`, `grass_05.jpg`, `rockGrass`, `rockNormal`, `rockSpireNormal`, `rockwall_01`, `rockWall_06`, `bushLeaf_3`, three `cabbageTree*`, `mushroomBlue`, `treeStump`, `treeBoundary`). Names such as `grass_05`, `rockwall_01` and `rockWall_06` read like a numbered texture pack; the repo does not say which.
- Skybox: `right`, `left`, `top`, `bottom`, `front`, `back` `.png`, 256 x 256.
- UI art: `You_Win.png` (683 x 293), `Bad_Luck.png` (848 x 425), both used; `HEAD_FOR_THE_EXIT.png`, `Score_Graphics.png`, `Time_Graphics.png`, `Bird_Collectable_Graphic_1.png`, `Bird_Collectable_Graphic_2.png`, `smile.png`, never referenced in code. The two used images arrived in the last commit before the merge (`c164f47` "adding images").
- `missing_texture.png`: fallback.

**Shaders** (`assets/Shaders/`, GLSL 4.60): `toon`, `skybox`, `debug` pairs in use; `default`, `lit`, `unlit` pairs unused.

**Animations:** none. **Fonts:** none (ImGui's built-in font). **Sounds:** `assets/sounds/step.wav`, 1,264 bytes, 8-bit mono 11,025 Hz, never loaded; there is no audio code.

**Not the game:** `assets/data/` (85 MB: 208 `.raw` textures, images, a 45-frame `custom/gif/`, `windows/`) and `lib/Amaxi_Kosmos` (submodule) are Shay's World.

## HUD and menus

- **HUD:** one default dark-style Dear ImGui window titled "Game", forced to 300 x 300, with four lines of text held in a `std::map`, so in alphabetical order: "Friends Remaining: n", "Lives Remaining: n", "Score: n", "Time Remaining: n" (`Game/Game.cpp:17-18,51-54`, `src/GUI/GUI.cpp:33-40`, `src/GUI/GUI.hpp:71`). The time is a bare integer starting at 200, not MM:SS. No health bar, crosshair, power-up indicator or objective prompt.
- **End screens:** the same window resized to 1270 x 960 with the win or lose image (`Game/Game.cpp:204-215`).
- **Menus:** none. No title screen, pause, options or level select.

## Known problems

Admitted in the code or history:

- "no check for grounded due to lack of time" (`Game/Player/Player.cpp:37`).
- Power-ups: "should be alteration to jump. no time" (`Game/Game.cpp:168`).
- "this is ugly but no time to refactor" (`Game/Game.cpp:153`); "this isn't ideal but its a quick fix" on the exit zone (`Game/Game.hpp:116`); "Adding but to be removed" on the coin and power-up enum values (`src/ECS/Components.hpp:172`).
- "not gonna bother to be honest" for convex-mesh colliders (`src/Physics/Physics.cpp:108`).
- The game was written in the last three days: the first game-logic commits are 2022-11-02 ("Shitty logic to free friends" `9563c21`, "adding in cages" `e462ffd`), coins arrive 2022-11-04 (`2b45857`), and the day ends with "Final push. Completely works" (`e768a76`), "Final bits of logic" (`be8bf97`), "Extra Logic" (`010989d`). Other messages from that day: "trying to resolve quaternions", "idk mann", "uygh".

Found by reading, not admitted:

- Coins cannot be collected with the committed scene (see The loop).
- The timer counts frames (see The loop).
- The win score multiplies every frame and overflows (`Game/Game.cpp:206`).
- Camera direction vectors: `_cameraYaw = glm::radians(M_PI - _cameraAngle)` converts a radian value as if it were degrees (`src/Resources/Camera/ThirdPersonCamera.cpp:110`), and `UpdateCameraVectors` converts again (lines 143-145). By the arithmetic the front vector stays within a fraction of a degree of world +X whatever the orbit angle, so W would push along a fixed world axis while the camera and the model turn. The commit messages claim otherwise ("dirs correct (not rot yet)" `e06f1cd`), so treat this as unverified without running it.
- Movement by `setTransform` bypasses the solver, so the capsule can be pushed into geometry and is corrected only by contact resolution afterwards.
- `Game::OnUpdate` dereferences the player entity even if none was found (`Game/Game.cpp:83,108`).
- The skybox and the physics debug pass are drawn once per entity, inside the entity loop (`src/Renderer/Renderer.cpp:136-232`); the entity list is re-sorted by camera distance every frame (lines 131-134).
- The asset loader splits paths on a backslash, so it only works on Windows (`src/AssetLoader/AssetLoader.cpp:39,54,71`), and reads `assets/models` in lower case (line 65).
