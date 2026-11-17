# AnimationDemo

The animation spec's demo app (`GLOSSARY.md`: demo app): skeletons, clip playback and GPU skinning on the demo
scaffold (`examples/DemoScaffold/README.md`), one scene per feature. Built with the examples when the renderer is on;
FenrirSkeleton and FenrirAnimation come with FenrirAssets, so the app needs no option of its own. It installs
`Skeletons` and then `Animation` itself, through the scaffold's `DemoConfig::installPackages`, after the scaffold's own
packages (FenrirAssets among them) and before Scripting, so the C# scenes' pose stage systems find the pipeline when the
scripts load; the renderer finds the skinning source `Skeletons` registers (ADR-0041). With scripting built
(`FENRIR_ENABLE_SCRIPTING`) it adds scenes 8 and 9, the C# twins of Playback and Bone attachments (`scripts/`, ticket 13),
run with the skeleton and animation extensions as PhysicsDemo's C# scenes are with physics's. With `FENRIR_ENABLE_PHYSICS` it also installs FenrirPhysics, before
the scaffold, for scene 5's shield and boxes, and adds that scene's `Tick` glue.

Its models (`assets/ATTRIBUTION.md`): Quaternius's CC0 human from ActionForge's animation library, a skinned mannequin
whose clips are files of their own on the same skeleton (they bind to it by bone name); Khronos's `Fox`; Khronos's
`CesiumMan`, untextured; Kenney's CC0 animated character, the FBX model (ticket 06), its run clip a file of its own
and its skin painted on in code; and two procedural models, `mannequin.gltf` (a blocky humanoid with a short Flutter clip, used
for the event budget and scene 3) and `critter.gltf` (a four-legged skinned model, scene 3's wrong-skeleton case).

## Scenes

| Scene | What it shows | Keys | Tickets |
| --- | --- | --- | --- |
| Playback | The Quaternius human, whose `Animator` the keys drive, its clip events as green world text above its head for a second each (`Step.A`/`Step.B` on the walk, `Stride.A`/`Stride.B` on the jog, `Dash` on the sprint, `Breathe` on the idle, `Hey!` on the call), and `ClipFinished` as a message when a `Once` clip ends. The walk, jog and sprint carry root motion, so it travels forward through each cycle and steps back to the start when the clip wraps. Beside it the procedural mannequin plays Flutter, a quarter-second clip given 600 events, at speed 2: about 80 crossings a frame against the 64-event budget, so the text above it shows 64 sent and the `AnimationEventsDropped` count each frame, and the drop is logged once as a warning. The demo panel's Walker section shows the walker's clip, time, speed, Loop or Once, paused or playing, finished and laps. The first scene: its tutorial shows on its first entry. | 1-5: Idle, Walk, Run (the jog), Sprint, Call (the library has no wave). Up/Down: speed up or down by 0.25. R: reverse. L: Loop or Once. Space: pause or play. Left/Right: seek 0.25 s back or on. Home: seek to 0. | 04, 05 |
| Skeleton | Five skinned models in a row, each labelled: the Quaternius human, the procedural mannequin, CesiumMan and the Fox (drawn at a hundredth of its size), all walking, and Kenney's character from its FBX file (drawn at about half its size), running. Their bones are drawn through FenrirDebugDraw on top of the meshes: a yellow line from each bone to its parent, small axes at each joint, and each bone's engine name as world text. | M: show or hide the meshes. B: the bones and axes. N: the names (off at first). | 03, 05, 06 |
| Skin diagnostics | A row of five mannequin-sized slots, each labelled from above with a ring on the floor, the label placed by its slot so it shows when nothing is drawn there: a working skinned mannequin playing Idle; one whose mesh entities have no `SkinnedMesh` (not drawn); one whose `SkeletonPose` names no skeleton, so it is `Unavailable` (not drawn); the critter's mesh skinned by a skeleton entity posing the mannequin's skeleton, whose bind signature differs (not drawn); and a mannequin waving in a user shader built without `SKINNED`, drawn magenta by the fallback material. Each skipped case logs one warning, never an error. | None. | 05 |
| Blending | Three Quaternius humans, labelled. Left: its `Animator` crossfades (0.8 s) through idle, walk, run and sprint, smooth or linear, and I changes clip then changes again 0.3 s later, mid-fade, so the second fade starts from a snapshot of the first. Middle: the walk, with the call played over it by an override `AnimationLayer` masked to `spine_01`'s subtree, so the arms and head call out while the legs keep walking. Right: the idle, with an additive layer leaning the upper spine 10 to 30 degrees and back every 2 s, measured from the clip's start (a sway from upright) or from rest (10 degrees further throughout); its weight can ramp to 0 over 1 s, hold there 2.5 s and ramp back over 1 s while its clock runs on, and its `Sway` event shows above its head only while its weight is above 0. The demo panel shows each one's state, and the scene's tutorial walks through the three on its first entry. | C: crossfade to the next clip. V: smooth or linear. I: crossfade, then interrupt it mid-fade. U: the upper-body call on or off. A: the lean from its start or from rest. W: ramp the lean's weight to 0 and back. | 08 |
| Bone attachments | The Quaternius human idling, C stepping it through the walk, jog, sprint and call, with three `BoneAttachment`s, each labelled with its `AttachmentState`: a rifle in its right hand (`hand_r`), itself a skeleton entity with a stock and a rail bone; a scope attached to the rifle's rail, an attachment on an attachment, placed after the rifle each frame; and a hat on its head. To its right, five amber balls on rings, one per failure case, each labelled with what is wrong and its state: a destroyed skeleton entity (`SkeletonMissing`), a `SkeletonPose` naming no skeleton (`SkeletonUnavailable`), a bone the human lacks (`BoneNotFound`), an entity with no `Transform`, so no ball (`TransformMissing`), and a ball attached to a skeleton parented under it, a cycle (`Unplaceable`). Each keeps its place and is logged once as a warning. W detaches the rifle with `KeepWorld`, so it stays where it was, the scope still on it; K detaches the hat with `KeepLocal`, so it hangs where the head last was; R attaches both again. To the left, a second Quaternius human sprints in place (the sprint on an `AnimationLayer` masked to `pelvis`'s subtree, so its root motion is left out) with an orange shield on its right forearm: a kinematic body whose entity has no `BoneAttachment`. Demo glue in `Tick` writes its `Transform` from `GetBoneWorldMatrix(…, lowerarm_r, PoseKind::Simulation) × offset`, and physics's `PostTick` moves it there, so each swing knocks the blue boxes off the table in front of it; the boxes are set out again every 5 s. Both humans start tick-posed (`PoseTiming::Tick`, ADR-0040): the bearer's simulation pose is evaluated in each tick, so the shield is on this tick's arm. T switches both to frame timing, where the same read gives last frame's render pose, so the shield lags a frame, and back. Built without physics (`FENRIR_ENABLE_PHYSICS=OFF`), the shield still follows the arm and the boxes stand still. | C: the next clip. W: drop the rifle (`KeepWorld`). K: leave the hat (`KeepLocal`). R: attach both again. T: tick or frame timing for both humans. B: set the boxes out again. | 10, 11 |
| Going limp | The Quaternius human walking, its left arm overridden through FenrirSkeleton's `PoseOverride`: demo code standing in for ragdoll glue writes world targets each frame in `Update` that hang the upper arm, forearm and hand from the shoulder, swaying, each placed at the end of its parent. The override weight on `upperarm_l`'s subtree steps to 1 to go limp and ramps to 0 over 1.5 s to blend back, the targets cleared once it reaches 0. The fingers have no targets and follow the hand; the rest of the body keeps walking. The demo panel's Left arm section shows the weight, whether targets are written, the state and the human's timing. | L: go limp. B: blend back into the walk. S: the sway on or off. T: tick or frame timing for the human. | 09, 11 |
| Crowd | A grid of Kenney's character, 64 at first: the members take the idle and the run in turn, each starting at its own point of its clip through `Animation::Seek`, so no two neighbours move together. Doubling adds a block of the grid beside the old one and halving removes the newest half, between 1 and 1024. The demo panel shows the instances and their total bones, the renderer's palette stats (`distinctPalettesWritten`, `paletteBytesUploaded`, `skinnedDraws`), and in its Frame section the frame time, draws and instances: one palette per member however many cameras see it, so the cost follows skeletons, not draws. Each member's mesh is skinned once a frame in compute (ADR-0050) unless C turns compute skinning off, which the panel shows; the renderer keeps the setting when another scene starts. No pass/fail thresholds. | Up: double the crowd. Down: halve it. Space: pause or play every member. C: compute skinning on or off. R: reset to 64, restarted. | 07 |
| Playback (C#) | Built only with scripting. Scene 1's human, made and driven entirely from C# (`scripts/Playback.cs`): `Renderer.SpawnModel` spawns it (with its `SkinnedMesh`es and `SkeletonPose`), its clips load by label (`walk_loop.glb#Clip0`), and C# writes its `Animator`. A clip change crossfades over 0.4 s. Its clip events, authored from C# with `Clips.AddEvent` (`C# Step.A`, `C# Step.B` and the rest), are read through `EventReader<AnimationEvent>` and shown as green world text above its head, `ClipFinished` as a message. One `AnimationLayer` plays the call over the upper body through a `BoneMask` made with `BoneMasks` (`spine_01`'s subtree), its weight ramped in and out. Its readouts, a Razor panel (`scripts/PlaybackPanel.razor`) in the scene's demo slot, are `Animation.Playback`'s: clip, time, speed, Loop or Once, paused or playing, laps and the crossfade's progress, and the layer's weight. | 1-5: crossfade to Idle, Walk, Run (the jog), Sprint, Call. Up/Down: speed. R: reverse. L: Loop or Once. Space: pause or play. Left/Right: seek 0.25 s back or on (`Animation.Seek`). Home: seek to 0. M: ramp the upper-body layer in or out. | 13 |
| Bone attachments (C#) | Built only with scripting. The walking human with three `BoneAttachment`s C# makes and edits (`scripts/BoneAttachments.cs`), each labelled with `Skeletons.GetAttachmentState`: a grey blade on `hand_r`, a blue hat on `Head`, and an amber tag naming `no_such_bone`, which stays `BoneNotFound`. Its readouts, a Razor panel (`scripts/AttachmentsPanel.razor`) in the scene's demo slot, show bone reads: the bone count (`SkeletonData.BoneCount`), the pose state and generation, and `Head`'s world position (`GetBoneWorldMatrix`), with the nod and the blade's and hat's states. Two stage systems run inside the pose pipeline: a `Fenrir.Skeleton.Modify` system at order 1010, after the pose override, nods the head through a `PoseEdit`; a `Fenrir.Skeleton.Late` system draws a pink dot on `hand_r` where this frame's pose put it. | W: detach the blade keeping its world place (`KeepWorld`). K: detach the hat keeping its local transform (`KeepLocal`). R: attach both again. Left/Right: slide the blade along `hand_r`'s X (its `offset`). N: the C# nod on or off. C: the next clip (idle, walk, jog). | 13 |

| Key | What it does |
| --- | --- |
| F1, F3, PageDown, PageUp | The scaffold's: the demo UI kit, the free-fly camera, the next and previous scene. |

Each scene's keys are its key rows (`DemoScene::keyRows`), which the help at the bottom left lists under the scaffold's.

## The demo panel

The scaffold's demo UI kit (`examples/DemoScaffold/README.md`) shows the readouts on the right, under the Scenes
switcher and the Frame section (`DemoConfig::frameStats`: frame time, fps, draws, instances and shadow draws). Each
scene's sections are its own, there while it is, pulled each frame (`DemoSection::Readout`):

| Scene | Sections and readouts |
| --- | --- |
| Playback | Playback (its Tutorial button). Walker: Clip, Time (time / duration), Speed, Loop, Playing (playing or paused, and finished), Laps. |
| Skeleton | Shown: Meshes, Bones, Names (on or off). |
| Skin diagnostics | Skip cases: a line saying each skip case logs one warning. The scene has no keys; its one key row says so. |
| Blending | Blending (its Tutorial button). Crossfades: Clip, Curve, Fade (its progress, or settled). Upper body: Call weight, Mask. Lean: Additive from, Weight, Clock (time and lap), Sway events. |
| Bone attachments | Attachments: Clip, Rifle, Hat, Timing, Boxes. |
| Going limp | Left arm: Subtree, Override weight, Targets, Arm (animated, limp or blending back), Sway, Timing. |
| Crowd | Crowd: Instances, Bones, Playing, Compute skinning. Palettes: Distinct written, Bytes uploaded, Skinned draws. The frame time, draws and instances are the Frame section's. |
| Playback (C#) | Its demo slot, where `scripts/PanelSetup.cs` mounts `PlaybackPanel.razor`. Walker (Razor): the C++ Walker's readouts, then Crossfade and Upper-body layer. |
| Bone attachments (C#) | Its demo slot, where `scripts/PanelSetup.cs` mounts `AttachmentsPanel.razor`. Bone reads (Razor): Bones, Pose, Generation, Head at, Nod, Blade, Hat. |

The C# twins' panels have no switches, only readouts: their keys are the scenes'. Clip events stay world text,
`ClipFinished` stays an on-screen message, and the world text labels stay.

Two scenes have a demo tutorial, shown on their first entry in a run (never in a smoke run or with `FENRIR_DEMO_KEYS`)
and again from their Tutorial button:

- **Playback**, the first scene: the scene list, F1, the free-fly camera and the scene's keys, stepped by Next.
- **Blending:** the crossfade (C or I), the upper-body layer (U) and the lean (A or W), each step ending when one of its
  keys is pressed.

## Smoke runs

The scaffold's `AnimationDemo-Smoke-null` (everywhere) and `AnimationDemo-Smoke-opengl` (label `gl`; on llvmpipe in the
Linux CI job when it runs) run every scene for 60 frames with its keys pressed, and fail on any error. Scene 3's
diagnostics are warnings (ADR-0035), and so are scene 5's attachment failures (ADR-0037), so both pass with no exceptions. With scripting built
the runs include scenes 8 and 9, and without it the list ends at the Crowd. `FENRIR_DEMO_SCENE="Skin diagnostics"
FENRIR_DEMO_KEYS=1` runs one scene through its keys. With scripting, `AnimationDemo-PlaybackRazor-null` and
`AnimationDemo-BoneattachmentsRazor-null` run each C# twin alone and check its log for its Razor panel mounting in its
demo slot (`cmake/CheckRazorPanelsLog.cmake`).

## Hand checks

Every scene draws through the camera's default tonemapper, AgX (ADR-0049), checked by hand in Playback on 2026-10-31:
the human, the floor and the world text read as before, a little softer and less saturated, as AgX keeps colours of 1
short of white.

Run each scene on OpenGL (`FENRIR_DEMO_SCENE=<scene>`, or PageDown through the list) and check:

- **The demo panel:** on the right, the Scenes switcher enters any scene on a click, and the Frame section's numbers
  change. Each scene's sections (above, "The demo panel") are there while it is and gone after PageDown, and no screen
  text is drawn at the top left but the on-screen messages. F1 hides the panel and the help, and shows them again.
- **Tutorials:** started without `FENRIR_DEMO_SCENE`, Playback's tutorial card shows at the top centre; Next steps it
  through its four steps, and Done closes it. Leaving Playback and coming back does not show it again; its Tutorial
  button does, from step one. Blending's shows on its first entry: C (or I) moves it on, then U, then A (or W) closes
  it; it does not show on a second entry.

- **Playback:** the Quaternius human walks forward a step or two and starts again at each wrap, with `Step.A` and
  `Step.B` above its head in turn, its fingers and feet moving with the clip. 3 jogs, 4 sprints, 5 calls out, 1 idles. Up and Down
  change the pace; R plays the clip backwards (events still show, in reverse order); Space freezes it mid-pose and plays
  on; Left, Right and Home jump to a new pose without showing the events between. L then a `Once` clip: it plays to its
  end, holds there, and shows `ClipFinished`. Beside it the second mannequin's hands flutter, and the text above it shows
  64 sent and a nonzero `AnimationEventsDropped` count each frame, with a running total. The Walker section follows each key: Clip, Speed (negative after R), Loop, Playing (and
  `finished` at a `Once` clip's end), and Time and Laps counting on.
- **Skeleton:** the Quaternius human, the mannequin, CesiumMan and the Fox stand upright on the floor at a person's and a fox's size, facing
  roughly the camera, and walk in place without torn or collapsed vertices. Kenney's FBX character, on the right, stands the
  right way up at a person's size, painted with its skater skin, and runs in place with its bones drawn over it, without
  torn or collapsed vertices. The yellow bone lines and axes sit inside
  each body and move with it; M hides the meshes leaving the bones walking, B hides the bones, N shows each bone's name at
  its joint; the Shown section's Meshes, Bones and Names follow each key. The Fox has no normals in its file, so it is lit flat.
- **Skin diagnostics:** five labels in a row. Only the first slot (moving gently) and the last (magenta, waving) draw a
  model; the middle three show their labels and rings over an empty floor. The log has one warning for each of the four
  cases, and for the Unavailable skeleton FenrirSkeleton's own warning too. The Skip cases section shows its line.
- **Blending:** on the left, C fades smoothly from one clip to the next with no pop, the feet and arms easing between the two cycles over most of a second; V makes the next fade linear (an even blend rather than easing in and out); I's second change, mid-fade, carries on from the half-blended pose without a jump. In the middle, the arms and head play the call over the walk while the legs walk on, and U drops the call at once, back to the plain walk. On the right, the upper body leans and sways over the idle; A moves the sway between upright-to-20 degrees and a further 10 degrees over, with no jump in rhythm; W eases the lean out and back in, and the lean returns in step with where it would have been, the `Sway` text skipping the sway that comes while the weight is 0 and coming back after, the Lean section's Sway events not rising then. The Crossfades section's Fade counts up while a fade runs and reads `settled` after; Curve follows V, Call weight follows U (1 or 0), and the Lean section's Additive from and Weight follow A and W.
- **Bone attachments:** the rifle stays in the right hand through every clip C steps to, the scope stays on top of the rifle wherever it goes, and the hat stays on the head; their labels say `Attached`. The five failure labels name `SkeletonMissing`, `SkeletonUnavailable`, `BoneNotFound`, `TransformMissing` and `Unplaceable`, their balls (none for `TransformMissing`) standing still, and the log has one warning for each. W leaves the rifle exactly where it was, in the air, with the scope on it, while the human moves on; K leaves the hat floating where the head was; R puts both back. On the left, the shield stays on the bearer's forearm through every stride and pushes the boxes off the table with no visible lag while the Attachments section's Timing says `Tick`; T (`Frame`) makes it trail the arm by a frame, easiest to see when the frame rate is low, and T again brings it back onto the arm. Between ticks the character and its shield move together, with no stepping at the tick rate. While tick-posed, the rifle, scope and hat still follow the first human as before, and W, K and R still work. The Attachments section's Clip, Rifle (`dropped` after W) and Hat (`left behind` after K) follow the keys, and R puts them back.
- **Going limp:** L drops the left arm at once to hang from the shoulder, swaying, with the fingers following the hand and the legs, body and other arm walking on undisturbed. S stops and restarts the sway. B blends the arm back into the walk over a second and a half with no pop, the Left arm section counting the Override weight down to 0, Arm reading `blending back` and then `animated`, and Targets `none`. Sway follows S. After T (Timing `Tick`), L, S and B behave the same, the walk drawn smoothly between ticks.
- **Crowd:** 64 characters in an 8 by 8 grid, painted with the skater skin, half idling and half running, visibly out of
  step with each other. Up four times reaches 1024 (32 by 32) without a crash, and Down ten times halves it back to 1;
  through each step the Crowd section's Instances follow, the Palettes section's Distinct written equals the instance count, Skinned draws equals it too (one
  mesh each, one camera), and Bytes uploaded is the instances times 58 bones times 48 bytes. With compute
  skinning on, each member is a draw of its own skinned vertices, so the Frame section's Draws equals the instances; C turns it off, and
  the crowd looks exactly the same, every pose and every lit fold of the skin, while the members merge into a handful of
  draws (ADR-0051); C again turns it back on. Space freezes every member mid-pose and plays on; R brings back the 64,
  moving again from their staggered starts.
- **Playback (C#):** the human walks as in Playback, with `C# Step.A` and `C# Step.B` above its head in turn. 1-5 change
  its clip with a visible crossfade (the Walker (Razor) section's Crossfade shows its progress, then `settled`) rather
  than a cut. Up, Down,
  R, L, Space, Left, Right and Home behave as scene 1's keys do; a `Once` clip ends with the `ClipFinished (C#)`
  message. M brings the call into the arms and head over half a second while the legs keep the clip, and M again eases it
  back out; the panel's Upper-body layer weight follows. The Razor
  readouts follow every key as the C++ Walker's do in Playback.
- **Bone attachments (C#):** the blade stays in the right hand and the hat on the head through every clip C steps to,
  both labelled `Attached`; the tag beside the human stays put, labelled `BoneNotFound`, with one warning in the log. The
  head nods up and down over the walk (the C# `Modify` system), and N stops and restarts the nod, the hat nodding with
  it. The pink dot sits on the right hand with no lag (the C# `Late` system). W leaves the blade in the air where it was;
  K leaves the hat where the head last was; R puts both back. Left and Right slide the blade along the hand while it
  stays attached. The Bone reads (Razor) section shows 71 bones, `Ready`, a generation that keeps rising, and the
  head's position moving with the walk; Nod follows N, Blade and Hat follow W, K and R, and the blade's offset follows
  Left and Right. Built without scripting, the two C# scenes are not in the list.
- **Lighting:** the skinned limbs are lit on their moving faces as the floor is, with no faces going dark or flashing as
  they turn.
