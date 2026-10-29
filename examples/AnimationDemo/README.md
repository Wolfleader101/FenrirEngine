# AnimationDemo

The animation spec's demo app (`GLOSSARY.md`: demo app): skeletons, clip playback and GPU skinning on the demo
scaffold (`examples/DemoScaffold/README.md`), one scene per feature. Built with the examples when the renderer is on;
FenrirSkeleton and FenrirAnimation come with FenrirAssets, so the app needs no option of its own. It installs
`Skeletons` and then `Animation` itself, after the scaffold (which installs FenrirAssets), and the renderer finds the
skinning source `Skeletons` registers (ADR-0041).

Its models (`assets/ATTRIBUTION.md`): `mannequin.gltf`, a procedural blocky humanoid with Idle, Walk, Run, Wave and
Flutter clips on its own rig, which scenes 1 and 3 use; `critter.gltf`, a procedural four-legged rig with a wagging tail
(scene 3's wrong-skeleton case); Khronos's `Fox`; and Khronos's `CesiumMan`, untextured. The spec's Quaternius humanoid
waits for a fully skinned CC0 file; scene 2's FBX character is ticket 06's.

## Scenes

| Scene | What it shows | Keys | Tickets |
| --- | --- | --- | --- |
| Playback | A mannequin whose `Animator` the keys drive, its clip events as green world text above its head for a second each (`Step.L`/`Step.R` on Walk, `Stride.L`/`Stride.R` on Run, `Breathe` on Idle, `Hello`/`Bye` on Wave), and `ClipFinished` as a message when a `Once` clip ends. Beside it a second mannequin plays Flutter, a quarter-second clip given 600 events, at speed 2: about 80 crossings a frame against the 64-event budget, so the text above it shows 64 sent and the `AnimationEventsDropped` count each frame, and the drop is logged once as a warning. The screen text shows the walker's clip, time, speed, Loop or Once, paused or playing, finished and laps. | 1-4: Idle, Walk, Run, Wave. Up/Down: speed up or down by 0.25. R: reverse. L: Loop or Once. Space: pause or play. Left/Right: seek 0.25 s back or on. Home: seek to 0. | 04, 05 |
| Skeleton | Three skinned models in a row, each labelled: the walking mannequin, CesiumMan walking, and the Fox walking (drawn at a hundredth of its size). Their bones are drawn through FenrirDebugDraw on top of the meshes: a yellow line from each bone to its parent, small axes at each joint, and each bone's engine name as world text. | M: show or hide the meshes. B: the bones and axes. N: the names (off at first). | 03, 05 (06 adds the FBX character) |
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

- **Playback:** the mannequin walks in place, its legs and arms swinging and its hips bobbing, with `Step.L` and
  `Step.R` above its head in turn. 3 runs, 4 waves (the right arm raised, the forearm waving), 1 idles. Up and Down
  change the pace; R plays the clip backwards (events still show, in reverse order); Space freezes it mid-pose and plays
  on; Left, Right and Home jump to a new pose without showing the events between. L then a `Once` clip: it plays to its
  end, holds there, and shows `ClipFinished`. Beside it the second mannequin's hands flutter, and the text above it shows
  64 sent and a nonzero `AnimationEventsDropped` count each frame, with a running total.
- **Skeleton:** the mannequin, CesiumMan and the Fox stand upright on the floor at a person's and a fox's size, facing
  roughly the camera, and walk in place without torn or collapsed vertices. The yellow bone lines and axes sit inside
  each body and move with it; M hides the meshes leaving the bones walking, B hides the bones, N shows each bone's name at
  its joint. The Fox has no normals in its file, so it is lit flat.
- **Skin diagnostics:** five labels in a row. Only the first slot (moving gently) and the last (magenta, waving) draw a
  model; the middle three show their labels and rings over an empty floor. The log has one warning for each of the four
  cases, and for the Unavailable skeleton FenrirSkeleton's own warning too.
- **Lighting:** the skinned limbs are lit on their moving faces as the floor is, with no faces going dark or flashing as
  they turn.
