# What does the animation demo app show?

Type: grilling
Status: open
Blocked by: 23, 28, 30

## Question

Each feature area gets its own demo app on the shared scaffold (ticket 23). What does the animation demo app show?

- Which models and clips, from where (a permissively licensed glTF character), and where they live in the repo.
- Scenes: playback and looping, crossfades, layers and masks, a skinned mesh with its bones drawn through FenrirDebugDraw, bone attachments, a pose override going limp with no physics, a crowd to watch cost.
- C++ and C# scenes, as the other demo apps have.
- Smoke runs on the null backend and llvmpipe, and a headless run with no renderer that checks bone positions.
- A hand-check list.

## Comments

- From ticket 30 (ADR-0035): a skinned mesh without `SkinnedMesh` is skipped, not drawn rigid; a bind-pose preview through the owner is a candidate debug view for this demo (on the map's Not yet specified, not required). The skip diagnostics (no `SkinnedMesh`, unavailable skeleton, failed bind signature, shader without `SKINNED`) are worth a hand-check scene.
