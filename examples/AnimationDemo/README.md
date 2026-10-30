# AnimationDemo

The animation spec's demo app (`GLOSSARY.md`: demo app): skeletons, clip playback and GPU skinning on the demo
scaffold (`examples/DemoScaffold/README.md`), one scene per feature. Built with the examples when the renderer is on;
FenrirSkeleton and FenrirAnimation come with FenrirAssets, so the app needs no option of its own. It installs
`Skeletons` and then `Animation` itself, after the scaffold (which installs FenrirAssets), and the renderer finds the
skinning source `Skeletons` registers (ADR-0041).

Its models (`assets/ATTRIBUTION.md`): Quaternius's CC0 human from ActionForge's animation library, a skinned mannequin
whose clips are files of their own on the same skeleton (they bind to it by bone name); Khronos's `Fox`; Khronos's
`CesiumMan`, untextured; Kenney's CC0 animated character, the FBX model (ticket 06), its run clip a file of its own
and its skin painted on in code; and two procedural models, `mannequin.gltf` (a blocky humanoid with a short Flutter clip, used
for the event budget and scene 3) and `critter.gltf` (a four-legged rig, scene 3's wrong-skeleton case).

## Scenes

| Scene | What it shows | Keys | Tickets |
| --- | --- | --- | --- |
| Playback | The Quaternius human, whose `Animator` the keys drive, its clip events as green world text above its head for a second each (`Step.A`/`Step.B` on the walk, `Stride.A`/`Stride.B` on the jog, `Dash` on the sprint, `Breathe` on the idle, `Hey!` on the call), and `ClipFinished` as a message when a `Once` clip ends. The walk, jog and sprint carry root motion, so it travels forward through each cycle and steps back to the start when the clip wraps. Beside it the procedural mannequin plays Flutter, a quarter-second clip given 600 events, at speed 2: about 80 crossings a frame against the 64-event budget, so the text above it shows 64 sent and the `AnimationEventsDropped` count each frame, and the drop is logged once as a warning. The screen text shows the walker's clip, time, speed, Loop or Once, paused or playing, finished and laps. | 1-5: Idle, Walk, Run (the jog), Sprint, Call (the library has no wave). Up/Down: speed up or down by 0.25. R: reverse. L: Loop or Once. Space: pause or play. Left/Right: seek 0.25 s back or on. Home: seek to 0. | 04, 05 |
| Skeleton | Five skinned models in a row, each labelled: the Quaternius human, the procedural mannequin, CesiumMan and the Fox (drawn at a hundredth of its size), all walking, and Kenney's character from its FBX file (drawn at about half its size), running. Their bones are drawn through FenrirDebugDraw on top of the meshes: a yellow line from each bone to its parent, small axes at each joint, and each bone's engine name as world text. | M: show or hide the meshes. B: the bones and axes. N: the names (off at first). | 03, 05, 06 |
| Skin diagnostics | A row of five mannequin-sized slots, each labelled from above with a ring on the floor, the label placed by its slot so it shows when nothing is drawn there: a working skinned mannequin playing Idle; one whose mesh entities have no `SkinnedMesh` (not drawn); one whose `SkeletonPose` names no skeleton, so it is `Unavailable` (not drawn); the critter's mesh skinned by a skeleton entity posing the mannequin's skeleton, whose bind signature differs (not drawn); and a mannequin waving in a user shader built without `SKINNED`, drawn magenta by the fallback material. Each skipped case logs one warning, never an error. | None. | 05 |

| Key | What it does |
| --- | --- |
| F1, F3, PageDown, PageUp | The scaffold's: help, the free-fly camera, the next and previous scene. |

## Smoke runs

The scaffold's `AnimationDemo-Smoke-null` (everywhere) and `AnimationDemo-Smoke-opengl` (label `gl`; on llvmpipe in the
Linux CI job when it runs) run every scene for 60 frames with its keys pressed, and fail on any error. Scene 3's
diagnostics are warnings (ADR-0035), so it passes with no exceptions. `FENRIR_DEMO_SCENE="Skin diagnostics"
FENRIR_DEMO_KEYS=1` runs one scene through its keys.

## Hand checks

Run each scene on OpenGL (`FENRIR_DEMO_SCENE=<scene>`, or PageDown through the list) and check:

- **Playback:** the Quaternius human walks forward a step or two and starts again at each wrap, with `Step.A` and
  `Step.B` above its head in turn, its fingers and feet moving with the clip. 3 jogs, 4 sprints, 5 calls out, 1 idles. Up and Down
  change the pace; R plays the clip backwards (events still show, in reverse order); Space freezes it mid-pose and plays
  on; Left, Right and Home jump to a new pose without showing the events between. L then a `Once` clip: it plays to its
  end, holds there, and shows `ClipFinished`. Beside it the second mannequin's hands flutter, and the text above it shows
  64 sent and a nonzero `AnimationEventsDropped` count each frame, with a running total.
- **Skeleton:** the Quaternius human, the mannequin, CesiumMan and the Fox stand upright on the floor at a person's and a fox's size, facing
  roughly the camera, and walk in place without torn or collapsed vertices. Kenney's FBX character, on the right, stands the
  right way up at a person's size, painted with its skater skin, and runs in place with its bones drawn over it, without
  torn or collapsed vertices. The yellow bone lines and axes sit inside
  each body and move with it; M hides the meshes leaving the bones walking, B hides the bones, N shows each bone's name at
  its joint. The Fox has no normals in its file, so it is lit flat.
- **Skin diagnostics:** five labels in a row. Only the first slot (moving gently) and the last (magenta, waving) draw a
  model; the middle three show their labels and rings over an empty floor. The log has one warning for each of the four
  cases, and for the Unavailable skeleton FenrirSkeleton's own warning too.
- **Lighting:** the skinned limbs are lit on their moving faces as the floor is, with no faces going dark or flashing as
  they turn.
