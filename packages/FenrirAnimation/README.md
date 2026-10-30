# FenrirAnimation

Clip playback and blending for FenrirSkeleton's skeletons (ADR-0028, ADR-0034, ADR-0036): a game-owned `Animator` on a skeleton entity names a clip, `AnimationLayer` entities play more clips over it, and FenrirAnimation's sampler, in the pose pipeline's `Animate` stage, advances their playback cursors and crossfades, blends them into the base pose and sends their clip events as ECS events. Its runtime is its own, on glm (no ozz). It depends on FenrirSkeleton and FenrirAssets, links no library, has no GPU code, and is built with `FENRIR_ENABLE_ASSETS`, with no option of its own. FenrirRenderer and FenrirPhysics link nothing from it.

Tick-time playback for tick-posed skeletons (ADR-0040) is a later ticket's; they keep their base pose until then.

## Install

`Animation::Install(app, skeletons, AnimationConfig)` adds the sampler to the `Animate` stage of the `Skeletons` owner at `AnimateOrder::Clips` (0), and returns the move-only `Animation` owner. With a `Skeletons` owner that is not valid (FenrirSkeleton is not installed), or a second time in one app, it logs one error and returns an owner whose `IsValid()` is false, having added nothing. Destroy it before the `Skeletons` owner: once it is gone the sampler does nothing and every scene's playback state is dropped.

`AnimationConfig::maxEventsPerInstancePerFrame` (64) bounds the events sent per skeleton per frame.

## The Animator

`Animator { clip, speed = 1, loop = Loop, playing = true, fadeSeconds = 0, fadeCurve = Smooth }` is plain data that game code writes and FenrirAnimation never does. It holds no time: the playback cursor (a `double`), the completed loops, `finished`, pending seeks, key cursors and the binding cache live in a private scene resource, reconciled against the `Animator`s at each sampling pass.

- **The base pose.** While `clip` names a clip, the sampler writes the whole base pose each frame: the rest pose, then each of the clip's translation, rotation and scale channels on its own. A bone or channel the clip lacks keeps its rest value, so hand edits of the base pose do not persist. With no `Animator`, or an empty `clip`, the base pose is left alone. Procedural changes to an animated skeleton are pose modifiers.
- **The clock** advances by `Time::deltaTime × speed`: negative plays in reverse, 0 holds, and `playing = false` freezes the cursor while still sampling there. `Loop` wraps over any number of laps, counting `completedLoops` (saturating) in either direction; `Once` clamps at the end, or at 0 in reverse, and holds.
- **Completion.** A `Once` clip reaching its boundary becomes `finished` and sends one `ClipFinished`. `finished` resets on a clip change, a seek, or playing away from the boundary it finished at; pausing keeps it.
- **A clip change** restarts at 0, or at the duration for a negative speed; the frame's step then advances from there. With `fadeSeconds` above 0 it crossfades (below).
- **A zero-duration clip** (no key after 0) samples its first keys, keeps the cursor at 0, never wraps, sends its events at 0 once when it starts playing, and, as `Once`, finishes on its first playing evaluation.
- **`Animation::Seek(scene, entity, seconds)`** queues a seek applied at the entity's next evaluation, after any clip change, the latest pending seek winning. The time is clamped (`Once`) or wrapped (`Loop`) into the clip. It crosses nothing, resets `finished`, moves the pose while paused, and may come before the entity has an `Animator` or a skeleton instance. A non-finite time is ignored with a warning.
- **`Animation::Playback(scene, entity)`** gives `PlaybackInfo { state, time, completedLoops, finished, fading, fadeProgress }`: `state` is `Ready`, `Unavailable` (the skeleton is not `Ready`, or the clip handle does not resolve) or `Missing` (no `Animator`). `fading` and `fadeProgress` (curve-free, in [0, 1]) describe a running crossfade; `time`, `completedLoops` and `finished` are always the current clip's.
- **Root motion is not extracted**: a root bone's channel moves the bone relative to its skeleton entity.

## Layers and blending (ADR-0036)

- **`AnimationLayer { skeleton, order, weight, mode, mask, reference, clip, speed, loop, playing, fadeSeconds, fadeCurve }`** on any entity layers a clip onto the skeleton entity `skeleton`, which need not be its ECS parent. The base `Animator` comes first, then the layers by `order`, then by entity. `Seek` and `Playback` take the layer entity (an `Animator` on the same entity wins).
- **Composition.** Each layer blends, per channel, into the pose accumulated beneath it at an effective weight of `weight × mask weight(bone)` clamped to [0, 1], nothing normalised. The base resolves what its clip does not animate to rest; an override layer passes it through; an additive layer adds an identity delta. Override lerps translation and scale and nlerps rotation along the shortest arc. Additive adds `(sample − reference) × w` to translation, applies `slerp(identity, sample × inverse(reference), w)` before the accumulated rotation, and multiplies scale by `lerp(1, sample / reference, w)`; a scale component whose reference or sample is non-finite, whose `|reference| < 1e-6` or whose ratio is not positive adds nothing, logged once per binding.
- **The foundation.** With no base `Animator` (or one with no clip, or a stale one), layers apply over the retained base pose: hand-authored, or as last sampled. FenrirAnimation keeps that pose under its layers, so layers never compound over their own output, and takes a removed or silenced layer's contribution back.
- **Reference poses** are complete local poses, the clip at 0 (`ClipStart`) or rest (`RestPose`), cached per (clip, skeleton, mode) revisions and dropped when unused.
- **Crossfades.** A clip change with `fadeSeconds` above 0 fades by `fadeCurve` (`Linear`, or `Smooth`: smoothstep) of a progress that advances by unscaled frame time and freezes while paused; the outgoing clip keeps advancing on its own cursor and, if `Once`, holds at its end and sends nothing. A change during a fade freezes the blended layer value, with its channel coverage, as a snapshot and fades on from it (no transition stack). A `fadeSeconds` change applies to the next clip change. Fades are coverage-aware: where one side does not animate a channel it gives the underlying value (rest for the base), or the identity delta on an additive layer. On a layer, no clip → clip fades in and clip → no clip fades out, then clears; on the base, clip → no clip cuts, keeping the last pose, and no clip → clip fades from the retained pose. A seek during a fade moves only the current clip.
- **Weight and cost.** Weight never touches a clock or a fade. A layer at weight 0 is not sampled, nor is a bone at mask weight 0, but clocks, fades and event detection run; key cursors search again when sampling resumes.
- **Masks** bind per skeleton by engine name, cached per (mask, skeleton) revisions. A name matching no engine name selects nothing and is logged once per binding (as ambiguous when it matches several source names). A mask handle that does not resolve reaches no bone, logged once per layer.
- **Lifecycle.** A layer naming a missing or non-skeleton entity reads `Missing`, an unready one `Unavailable`; neither contributes nor advances. Changing `skeleton` restarts the layer with no fade; removing the component or the entity cuts its contribution at once, its state released at the next reconcile. Other fields apply at the next evaluation.

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

Across layers (ADR-0036) a clip's events fire only while its layer's weight is above 0 and the clip is fade-eligible: an incoming clip from its fade's start, inclusive; an outgoing one until its fade ends, exclusive, judged at each crossing's fraction of the frame's step; a snapshot never. Silent crossings are not replayed. A skeleton's eligible crossings from every layer are merged by frame fraction, ties by layer order (the base first), into the one per-skeleton budget, and the rest are counted per layer into `AnimationEventsDropped`, with the drop logged once per skeleton. `ClipFinished` describes the current clip only and is sent whatever the weight. `layer` names the layer entity, or the skeleton entity for the `Animator`.

Delivery is best-effort per frame.

## Tick-posed skeletons (ADR-0040)

A skeleton with `SkeletonPose::timing = PoseTiming::Tick` is sampled in FenrirSkeleton's tick passes, one per fixed tick, over its simulation pose, and not in the frame pass:

- **Clocks**, fades and seeks step by `Time::tickRate × speed` per tick that runs, so they keep step with physics: the ticks `Time::maxTicksPerFrame` drops advance nothing, as they move no body. A frame with no tick advances nothing.
- **Events** fire in the tick whose step crosses them, sent from that tick's `PreTick`, so a `Tick` reader sees them in the same tick. Delivery and the budget are per tick.
- **Commands** written before a `PreTick` are seen by that tick, in `Tick` by the next, in `Update` by the next frame's first tick.
- **Switching timing** keeps the playback state: the clock resumes in the other pass from where it stopped.

## Tests

`FenrirAnimationTests` (with `FENRIR_BUILD_TESTS`) is the spec's seam 9: install, ADR-0034's interval table and the event budget, seeks, completion, sampling against glTF's formulas, unavailable assets and recovery, revision rebinding, binding diagnostics and the cache, and (`Blending.cpp`) ADR-0036's composition, masks, additive maths and reference modes, crossfades with snapshots and coverage, weight against the clock, events across layers and the shared budget, and the layer lifecycle, and (`TickPosed.cpp`) tick-posed playback by `tickRate` with events and crossfades per tick, on skeletons and clips built in code, driven by `App::RunFrame`. The headless bone check plays `Tests/fixtures/rig.gltf` and compares bone world matrices with values worked by hand; `twins.gltf` checks binding by node identity. `Tests/fixtures/make_fixtures.py` wrote both. `PublicHeaders.cpp` fails the build if a header reaches the renderer, physics, a window, a graphics API or ozz, and configuring fails if FenrirRenderer or FenrirPhysics links the package.
