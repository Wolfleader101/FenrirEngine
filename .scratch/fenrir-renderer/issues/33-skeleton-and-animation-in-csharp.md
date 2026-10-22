# What does C# get from FenrirSkeleton and FenrirAnimation?

Type: grilling
Status: open
Blocked by: 22, 28, 29

## Question

Each package extends scripting through its own extension, built only when scripting is on (ADR-0009). What do FenrirSkeleton's and FenrirAnimation's extensions give C#?

- Reading bone poses (base, final, model space, world matrices) and bone lookup by name, without copying whole arrays each call.
- Writing modifier targets and weights (ADR-0028), and bone attachments.
- Playing, crossfading and layering clips; reading playback state; receiving clip events.
- How variable-length pose arrays cross the boundary (spans over native memory, as other extensions do or not).
- Which of this follows the renderer's scripting extension's choices (ticket 22).

## Comments

- From ticket 26 (ADR-0031): `SkeletonPose` is a blittable `{ AssetHandle<SkeletonData> }` C# can mirror; the arrays live in `SkeletonPoses` and cross by span-filling functions, valid only within the current system.
- From ticket 27 (ADR-0033): the pose pipeline's public stages are `Animate`, `Modify` and `Late`, ordered by an `int` within each; how a C# system or modifier joins a stage, and how `PoseEdit` (valid only during one modifier call) and `SetOverride` cross to C#, are this ticket's.
- From ticket 28 (ADR-0034): C# mirrors the blittable `Animator`; `Animation::Seek` and `Animation::Playback` cross as native functions; `AnimationEvent`, `ClipFinished` and `AnimationEventsDropped` are plain C++ events read through event readers.

- From ticket 30 (ADR-0035): `SkinnedMesh { Entity skeleton; }` is a blittable component beside `MeshRenderer` that C# mirrors; its documentation says prominently that a skinned mesh is drawn with the skeleton entity's world matrix and its own `Transform` is ignored. `PoseGeneration` may be worth exposing for C# caches.

- From ticket 29 (ADR-0036): C# mirrors the blittable `AnimationLayer` and `Animator`'s new `fadeSeconds` and `fadeCurve`, with the `LayerMode`, `AdditiveReference` and `FadeCurve` enums; `BoneMask` is a FenrirAssets asset built by name and subtree, which C# creates and edits as it does materials; `Seek` and `Playback` take a layer entity too, `Playback` gains `fading` and `fadeProgress`, and `AnimationEvent` and `ClipFinished` gain `layer`.
- From ticket 31 (ADR-0037): C# mirrors the blittable `BoneAttachment { Entity skeleton; BoneName bone; Transform offset; }`; `BoneName` is a fixed 256-byte UTF-8 buffer whose C# `Set` rejects names over 255 bytes, as C++'s does. `Skeleton::AttachmentState(entity)` and the transactional `Skeleton::Detach(entity, DetachMode, newParent)` cross as native functions; `Detach` is structural, never inside the pose pipeline.
