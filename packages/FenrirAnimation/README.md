# FenrirAnimation

Clip playback for FenrirSkeleton's skeletons (ADR-0028, ADR-0034): a game-owned `Animator` on a skeleton entity names a clip, and FenrirAnimation's sampler, in the pose pipeline's `Animate` stage, advances its playback cursor, writes the base pose from the clip and sends its clip events as ECS events. Its runtime is its own, on glm (no ozz). It depends on FenrirSkeleton and FenrirAssets, links no library, has no GPU code, and is built with `FENRIR_ENABLE_ASSETS`, with no option of its own. FenrirRenderer and FenrirPhysics link nothing from it.

This is the base layer only. Crossfades, `AnimationLayer`s and bone masks (ADR-0036) are a later ticket's, so a clip change cuts and `fadeSeconds` and `fadeCurve` are not used yet; so is tick-time playback for tick-posed skeletons (ADR-0040), which keep their base pose until then.

## Install

`Animation::Install(app, skeletons, AnimationConfig)` adds the sampler to the `Animate` stage of the `Skeletons` owner at `AnimateOrder::Clips` (0), and returns the move-only `Animation` owner. With a `Skeletons` owner that is not valid (FenrirSkeleton is not installed), or a second time in one app, it logs one error and returns an owner whose `IsValid()` is false, having added nothing. Destroy it before the `Skeletons` owner: once it is gone the sampler does nothing and every scene's playback state is dropped.

`AnimationConfig::maxEventsPerInstancePerFrame` (64) bounds the events sent per skeleton per frame.

## The Animator

`Animator { clip, speed = 1, loop = Loop, playing = true, fadeSeconds = 0, fadeCurve = Smooth }` is plain data that game code writes and FenrirAnimation never does. It holds no time: the playback cursor (a `double`), the completed loops, `finished`, pending seeks, key cursors and the binding cache live in a private scene resource, reconciled against the `Animator`s at each sampling pass.

- **The base pose.** While `clip` names a clip, the sampler writes the whole base pose each frame: the rest pose, then each of the clip's translation, rotation and scale channels on its own. A bone or channel the clip lacks keeps its rest value, so hand edits of the base pose do not persist. With no `Animator`, or an empty `clip`, the base pose is left alone. Procedural changes to an animated skeleton are pose modifiers.
- **The clock** advances by `Time::deltaTime × speed`: negative plays in reverse, 0 holds, and `playing = false` freezes the cursor while still sampling there. `Loop` wraps over any number of laps, counting `completedLoops` (saturating) in either direction; `Once` clamps at the end, or at 0 in reverse, and holds.
- **Completion.** A `Once` clip reaching its boundary becomes `finished` and sends one `ClipFinished`. `finished` resets on a clip change, a seek, or playing away from the boundary it finished at; pausing keeps it.
- **A clip change** restarts at 0, or at the duration for a negative speed; the frame's step then advances from there.
- **A zero-duration clip** (no key after 0) samples its first keys, keeps the cursor at 0, never wraps, sends its events at 0 once when it starts playing, and, as `Once`, finishes on its first playing evaluation.
- **`Animation::Seek(scene, entity, seconds)`** queues a seek applied at the entity's next evaluation, after any clip change, the latest pending seek winning. The time is clamped (`Once`) or wrapped (`Loop`) into the clip. It crosses nothing, resets `finished`, moves the pose while paused, and may come before the entity has an `Animator` or a skeleton instance. A non-finite time is ignored with a warning.
- **`Animation::Playback(scene, entity)`** gives `PlaybackInfo { state, time, completedLoops, finished, fading, fadeProgress }`: `state` is `Ready`, `Unavailable` (the skeleton is not `Ready`, or the clip handle does not resolve) or `Missing` (no `Animator`). `fading` is false and `fadeProgress` 0 until crossfades land.
- **Root motion is not extracted**: a root bone's channel moves the bone relative to its skeleton entity.

## Sampling

Keys are sampled raw, as glTF defines them: `Step` holds a key; `Linear` lerps translation and scale and slerps rotation along the shortest path; `CubicSpline` is glTF's Hermite form with the tangents scaled by the key interval, rotations normalised afterwards (tangents never are). Before a channel's first key its first value holds, after its last key the last value. Each channel keeps a key cursor, walked forward during forward playback and searched for after a seek, a reversal or a wrap. Keeping rotation keys in one hemisphere is the importer's (ADR-0034).

## Bindings

A binding maps a clip's channels to bone indices, once per (clip handle and revision, skeleton handle and revision), shared by every instance of the pair. It is built on first use, rebuilt when either revision changes, and dropped after any pass in which no instance used it. A clip binds to a skeleton loaded from the same file (`AssetStore::FromSameFile`) by node identity, and to any other by exact engine name. A channel that binds to nothing is not played, and is logged once per binding with the clip, the channel's target and path, and the reason: no bone matches, the match is ambiguous (the skeleton repeats the bone's source name), or the target is an ordinary node.

## Unavailable clips and skeletons

- A skeleton that is not `Ready` is neither sampled nor advanced; when it is `Ready` again, playback resumes from where it froze, with no events for the gap.
- A clip handle that does not resolve freezes the cursor and the pose (the base pose keeps its last sampled values), reads as `Unavailable`, and is logged once each time it stops resolving. A clip whose revision changes (`Replace`, a clip event edit) is rebound and its cursor clamped (`Once`) or wrapped (`Loop`) into its duration, crossing nothing; a finished clip stays finished at its boundary.
- The renderer is unaffected by clip availability.

## Events

Sent from the `Animate` stage through the app's event queues (ADR-0012), so `PostUpdate` readers after the pose pipeline see them the same frame and `Update` readers the next:

- **`AnimationEvent { skeleton, layer, clip, clipRevision, index, nameId, time, loop }`** for each clip event the cursor crosses: forward in (previous, current], reverse in [current, previous), in playback order; a wrap crosses the rest of the old lap, every whole lap, then the new lap's part, each event at 0 and at the duration crossing on each lap; when playback starts, its starting boundary is included. Seeks, clip changes, pauses, a speed of 0 and recovery cross nothing. `layer` is the skeleton entity for the `Animator`; `index` names an event only in `clipRevision`; `loop` is the lap the crossing happened in.
- **`ClipFinished { skeleton, layer, clip }`** when a `Once` clip becomes finished.
- **`AnimationEventsDropped { skeleton, layer, clip, dropped }`** when a frame crossed more than the budget: the earliest `maxEventsPerInstancePerFrame` are sent, and the rest are counted (whole laps times events per lap, plus the partial laps, never enumerated, saturating). The drop is logged once per skeleton instance.

Delivery is best-effort per frame.

## Tests

`FenrirAnimationTests` (with `FENRIR_BUILD_TESTS`) is the spec's seam 9: install, ADR-0034's interval table and the event budget, seeks, completion, sampling against glTF's formulas, unavailable assets and recovery, revision rebinding, binding diagnostics and the cache, on skeletons and clips built in code, driven by `App::RunFrame`. The headless bone check plays `Tests/fixtures/rig.gltf` and compares bone world matrices with values worked by hand; `twins.gltf` checks binding by node identity. `Tests/fixtures/make_fixtures.py` wrote both. `PublicHeaders.cpp` fails the build if a header reaches the renderer, physics, a window, a graphics API or ozz, and configuring fails if FenrirRenderer or FenrirPhysics links the package.
