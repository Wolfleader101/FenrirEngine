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
| Blending | Three Quaternius humans, labelled. Left: its `Animator` crossfades (0.8 s) through idle, walk, run and sprint, smooth or linear, and I changes clip then changes again 0.3 s later, mid-fade, so the second fade starts from a snapshot of the first. Middle: the walk, with the call played over it by an override `AnimationLayer` masked to `spine_01`'s subtree, so the arms and head call out while the legs keep walking. Right: the idle, with an additive layer leaning the upper spine 10 to 30 degrees and back every 2 s, measured from the clip's start (a sway from upright) or from rest (10 degrees further throughout); its weight can ramp to 0 over 1 s, hold there 2.5 s and ramp back over 1 s while its clock runs on, and its `Sway` event shows above its head only while its weight is above 0. The screen text shows each one's state. | C: crossfade to the next clip. V: smooth or linear. I: crossfade, then interrupt it mid-fade. U: the upper-body call on or off. A: the lean from its start or from rest. W: ramp the lean's weight to 0 and back. | 08 |
| Crowd | A grid of Kenney's character, 64 at first: the members take the idle and the run in turn, each starting at its own point of its clip through `Animation::Seek`, so no two neighbours move together. Doubling adds a block of the grid beside the old one and halving removes the newest half, between 1 and 1024. The screen text shows the instances, their total bones, the frame time and the renderer's palette stats (`distinctPalettesWritten`, `paletteBytesUploaded`, with the draws and `skinnedDraws`): one palette per member however many cameras see it, so the cost follows skeletons, not draws. No pass/fail thresholds. | Up: double the crowd. Down: halve it. Space: pause or play every member. R: reset to 64, restarted. | 07 |

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
- **Blending:** on the left, C fades smoothly from one clip to the next with no pop, the feet and arms easing between the two cycles over most of a second; V makes the next fade linear (an even blend rather than easing in and out); I's second change, mid-fade, carries on from the half-blended pose without a jump. In the middle, the arms and head play the call over the walk while the legs walk on, and U drops the call at once, back to the plain walk. On the right, the upper body leans and sways over the idle; A moves the sway between upright-to-20 degrees and a further 10 degrees over, with no jump in rhythm; W eases the lean out and back in, and the lean returns in step with where it would have been, the `Sway` text skipping the sway that comes while the weight is 0 and coming back after, the screen's event count not rising then.
- **Crowd:** 64 characters in an 8 by 8 grid, painted with the skater skin, half idling and half running, visibly out of
  step with each other. Up four times reaches 1024 (32 by 32) without a crash, and Down ten times halves it back to 1;
  through each step the text's `distinctPalettesWritten` equals the instance count, `skinnedDraws` equals it too (one
  mesh each, one camera), and `paletteBytesUploaded` is the instances times 58 bones times 48 bytes. Space freezes every
  member mid-pose and plays on; R brings back the 64, moving again from their staggered starts.
- **Lighting:** the skinned limbs are lit on their moving faces as the floor is, with no faces going dark or flashing as
  they turn.
