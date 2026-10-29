# FenrirSkeleton

Skeleton poses as arrays, with no animation, renderer or physics (ADR-0028, ADR-0031, ADR-0033, ADR-0041): skeleton instances and their reads in local, model and world space, the pose pipeline and its stages, pose modifiers through `PoseEdit`, the pose generation and the lazy skinning palette, the skinning source, and spawning. It depends on FenrirAssets, FenrirECS, FenrirScene, FenrirApp, FenrirMath and FenrirTime, links no library, and is built with `FENRIR_ENABLE_ASSETS`, with no option of its own. FenrirRenderer and FenrirPhysics link nothing from it: the renderer reaches poses only through FenrirAssets' `SkinningSource`.

## Install

`Skeletons::Install(App&)` returns the move-only `Skeletons` owner, the package's facade. It registers FenrirSkeleton's skinning source as the `SkinningSourceRegistration` app resource and adds the pose pipeline's system. Without FenrirAssets installed, or with a skinning source already registered (a second install), it logs one error and returns an owner whose `IsValid()` is false, having added nothing. Destroying the owner removes the source and every scene's instances; the system then does nothing.

## Skeleton instances

`SkeletonPose { AssetHandle<Skeleton> skeleton; PoseTiming timing = PoseTiming::Frame; }` is plain data on the skeleton entity, one per entity. The instance it names (base and final locals, model matrices, the cached palette) lives in a private scene resource.

- **Reconcile**, the pipeline's first step, makes an instance for a new component, rebuilds it at the rest pose when its handle or the asset's revision changes, and drops it when the entity or component is gone. `Ensure(scene, entity)` does that at once for one entity.
- **`GetPoseState`** is `Ready`, `Unavailable` (an empty or stale handle: every read empty, logged once each time it becomes so) or `Missing` (no instance yet, or none at all). On recovery the instance rebuilds.
- **Reads** are static, taking the scene and the skeleton entity: `FindBone` (by engine name), the spans `GetLocals`, `GetBaseLocals` and `GetModelMatrices`, per bone `GetLocal`, `GetModel`, `GetBaseLocal` and `GetBaseModel` (computed on first read and cached until the base pose changes), and `GetBoneWorldMatrix`, `GetWorldMatrix(entity) × M_model[bone]`. A span is valid until the end of the current system or the next change to the scene's instances.
- **`PoseKind`** is `Render` (the default) or `Simulation`. On a frame-posed skeleton both read the render pose; tick-posed skeletons, which tell them apart, are a later ticket's, and until then read the render pose too.
- **`EditBasePose(scene, entity)`** gives the base pose's locals to write and marks the instance pose-dirty, even if no value changes. Written before the pipeline, the change is this frame's; after it, the next frame's. It is refused (an empty span, logged once per call site) during `Modify` and `Late`, and during `Animate` on an instance of the other timing.
- **Three kinds of change stay apart:** a pose change dirties the model matrices and the palette; a move of the skeleton entity changes only world reads; an asset or revision change rebuilds the instance.
- **`GetPoseGeneration`** is advanced when finalisation changes the final pose and when the instance is rebuilt, from one counter per scene, so a recycled entity never repeats one. A move never advances it.
- **`GetPalette`**, main thread only, is `M_model × inverse bind` per bone, built on first read for the current generation and cached with it.

## The pose pipeline

One sequential `PostUpdate` system runs it on the active scene: reconcile, `Animate`, prepare (final pose from base pose), `Modify`, finalise (model matrices left, parents first), attach (bone attachments, a later ticket's) and `Late`. Inactive scenes pause, and resume without catching up.

- **Stages:** `AddAnimate`, `AddModifier` and `AddLate` add work at an `int` order, ties in the order added, and return a `PoseStageHandle` that `Remove` takes outside the pipeline (inside it, `Remove` is refused with an error). FenrirSkeleton's own modifiers use constants: `ModifierOrder::Override` is 1000. Nothing is promised against other `PostUpdate` systems.
- **`Animate`** items get an `AnimateContext`: the pass's `Timing()` (`Frame`; tick passes are a later ticket's), its `Step()` (`Time::deltaTime`), and the `Ready` `Instances()` of that timing, the only ones they may write.
- **`Modify`:** a modifier runs every frame, chooses its instances, and opens `PoseEdit edit = context.Edit(entity)`, valid only during that call and empty for an instance that is not `Ready`. It offers `Base()`, `Local(bone)`, `Model(bone)` and `Write(bone, Target)`.
- **`Target { space, trs, channels, weight }`** resolves when written: a `World` target through the inverse of the skeleton entity's world matrix (read at the instance's first edit of the frame, before any modifier may have moved it), a `Model` target through the inverse of the parent's current model matrix, then decomposed into TRS (shear dropped) and blended into the local by `weight` (clamped to [0, 1]) for the masked `Channels`: translation and scale lerp, rotation nlerps along the shortest arc. Writes compose in order; model matrices below a write are recomputed when read. A missing inverse skips the target, with a warning naming the entity and bone, at most once every 300 frames.
- **`Late`** items get a `LateContext` and read this frame's final poses; they may write ordinary ECS state, never a pose.
- **Cost:** an instance whose base pose is clean and that no modifier edited this frame or the last skips prepare and finalise, and keeps its generation.

## Spawning

`SpawnSkeletons(scene, model, spawned)` adds a `SkeletonPose` to each skeleton's owner (`Model::skeletonOwners`, an ordinary node or a synthetic owner) whose skeleton loaded, and `Ensure`s it. An owner that already has one keeps it, with a warning. `SpawnModel(scene, model, parent)` is `SpawnNodes` then `SpawnSkeletons`, for apps with no renderer. Giving ordinary nodes below bones a bone attachment is a later ticket's.

## The skinning source

FenrirSkeleton's `SkinningSource` answers the renderer per scene and entity: `GetPoseState`, `GetSkeleton`, `GetPoseGeneration`, `GetPalette`, `GetModelMatrices` and `SpawnSkeletons`, from the same reads. The renderer finds it with `app.TryGetResource<SkinningSourceRegistration>()`, whenever it needs it, so install order does not matter.

## Tests

`FenrirSkeletonTests` (with `FENRIR_BUILD_TESTS`) is the spec's seam 8: instances, generations and the palette, the pipeline, modifiers, spawning and install, on skeletons and models built in code, driven by `App::RunFrame`. `PublicHeaders.cpp` fails the build if a header reaches the renderer, physics, a window or a graphics API, and configuring fails if FenrirRenderer or FenrirPhysics links the package.
