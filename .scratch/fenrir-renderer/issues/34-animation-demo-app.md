# What does the animation demo app show?

Type: grilling
Status: resolved
Blocked by: 23, 28, 30

## Question

Each feature area gets its own demo app on the shared scaffold (ticket 23). What does the animation demo app show?

- Which models and clips, from where (a permissively licensed glTF character), and where they live in the repo.
- Scenes: playback and looping, crossfades, layers and masks, a skinned mesh with its bones drawn through FenrirDebugDraw, bone attachments, a pose override going limp with no physics, a crowd to watch cost.
- C++ and C# scenes, as the other demo apps have.
- Smoke runs on the null backend and llvmpipe, and a headless run with no renderer that checks bone positions.
- A hand-check list.

## Answer

No ADR (a demo app's content is cheap to reverse), no new tickets (68–70 unused), no glossary entries: the animation spec's own tickets carry it, as ticket 23's foundation tickets carry RendererDemo. Where a scene shows something tickets 29, 31, 32 or 33 decide, it is listed here by name only and depends on that ticket.

- **App:** **AnimationDemo**, one more app on ticket 23's scaffold (`fenrir_add_demo_app`, `examples/AnimationDemo/`), with an ordered scene list on PageUp/PageDown, F1 help, F3 free-fly camera, and its CTest smoke runs registered by `fenrir_add_demo_app` like every other app's (`FENRIR_DEMO_SMOKE` on the null backend everywhere and on llvmpipe in Linux CI); no smoke-run exceptions of its own.
- **Models:**
  - *Main humanoid:* a Quaternius CC0 glTF humanoid with its clips (idle, walk, run, wave and the like) from the same source rig, such as the *Universal Base Characters* with the *Universal Animation Library*: there is no retargeting, so clips are never assumed to fit another model's skeleton.
  - *Second rig:* the Khronos sample `Fox` (glTF, with its own Survey/Walk/Run), a non-humanoid showing that more than one skeleton works.
  - *FBX:* one Kenney CC0 animated character, so ufbx is checked by eye with a real rig.
  - All in `examples/AnimationDemo/assets/` with an `ATTRIBUTION.md` giving each asset's author, licence and source URL. The ticket that adds an asset checks its exact files and licence first (expected: Quaternius and Kenney CC0; Fox's model CC0 and its rig and animation CC-BY 4.0).
- **Scenes, in order:**
  1. *Playback:* `Animator` speed, reverse, `Loop`/`Once`, pause and seek on the main humanoid; clip events shown as world text over it; a short clip at high speed hitting the 64-per-frame event budget, with `AnimationEventsDropped` on screen.
  2. *Skeleton:* the skinned humanoid with its bones drawn through FenrirDebugDraw (lines parent to child, axes, names as world text), keys toggling mesh, bones and names; the Fox and the FBX character beside it.
  3. *Skin diagnostics:* one entity per skip case of ADR-0035 (no `SkinnedMesh`, an `Unavailable` skeleton, a failed bind signature, a user shader without `SKINNED` falling back to magenta), each labelled with world text at its place even when its draw is skipped, so the scene never looks empty or broken.
  4. *Blending:* crossfades, layers and masks. Depends on ticket 29.
  5. *Bone attachments:* attachments on the humanoid, including an attachment on an attachment and the failure cases. Depends on ticket 31.
  6. *Going limp:* a pose override going limp and back, with no physics. Depends on ticket 32.
  7. *Crowd:* a grid of the main humanoid, 64 to start, keys doubling and halving the count between 1 and 1024, clips and start times staggered by `Seek`; pause and reset keys; screen text with instance count, total bones, frame time and the renderer's palette stats (below). A visual and profiling aid, with no pass/fail thresholds.
- **C# scenes:** two twins, both depending on ticket 33. *Playback (C#)*: clip control, seeking, playback-state queries and clip events. *Bone attachments (C#)* (also on ticket 31): creating and editing attachment components, querying bone transforms and observing attachment resolution states. Blending gets a C# twin only if ticket 33 exposes blending.
- **Headless bone check:** not in the demo, which always installs the renderer. A FenrirAnimation integration test with no renderer plays a very small checked-in fixture (Khronos `RiggedSimple` or a hand-written rig) under the package's test data to known times and compares bone world transforms with analytic values, so a failure points at one bone and one time.
- **Not shown:** a bind-pose preview stays on the map's *Not yet specified*; the Skeleton scene's bone view covers debugging.
- **Diagnostics are warnings (for the animation and foundation specs):** expected configuration problems log as warnings, once each, and the smoke run allows warnings: a skinned mesh without `SkinnedMesh`, an unavailable skeleton, a failed bind signature or count check, a shader without `SKINNED`, and the magenta missing-material fallback (ADR-0023). Errors are left for failures of the engine's own contract: internal renderer failures, invalid GPU buffer access, failed shader compilation and the like. So scene 3 and RendererDemo's fallback hand checks pass their smoke runs without exceptions.
- **Palette stats (for the animation spec's skinning ticket):** the renderer's `Stats` (ADR-0023) gains `paletteBytesUploaded` (bytes actually copied into the frame's palette buffer, not capacity or allocations; GPU transfer traffic, if ever measured, is a separate stat), `distinctPalettesWritten` (palette ranges written after `PoseGeneration` deduplication) and `skinnedDraws` (skinned draw submissions). They reset each frame and cover every view drawn that frame; a skeleton drawn by several meshes or cameras with an unchanged `PoseGeneration` counts one upload, and a new generation counts another.
- **Order in the animation spec:** the ticket that first draws a skinned mesh (ADR-0035) creates AnimationDemo with scenes 1–3, its README and its smoke runs; the tickets for blending, bone attachments, the ragdoll handover and the C# extensions each add their scene (4, 5, 6, the C# twins) and hand checks as part of done; the crowd scene and the palette stats come with the skinning ticket or right after it.
- **README:** `examples/AnimationDemo/README.md` gives each scene its purpose, keys, expected behaviour, the tickets it depends on and the manual verification steps (the hand-check list).
- **Graduated:** nothing new. Comments appended to tickets 23 (diagnostics as warnings), 30 (warnings, palette stats) and 33 (the C# twins).

## Comments

- From ticket 28 (ADR-0034): show `Animator` playback (speed, reverse, `Loop`/`Once`, pause, seek), clip events, and the per-frame event budget on a short clip at high speed.

- From ticket 30 (ADR-0035): a skinned mesh without `SkinnedMesh` is skipped, not drawn rigid; a bind-pose preview through the owner is a candidate debug view for this demo (on the map's Not yet specified, not required). The skip diagnostics (no `SkinnedMesh`, unavailable skeleton, failed bind signature, shader without `SKINNED`) are worth a hand-check scene.
