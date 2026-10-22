---
status: accepted
---

# Clips blend in FenrirAnimation through layer entities, crossfades and bone masks

ADR-0034 plays one clip per skeleton through a game-owned `Animator`, and left to renderer map ticket 29 how that grows to crossfades, layers and several clips. ADR-0028 listed layers and masks among the pose modifiers to come, and ADR-0033 built modifiers as ordered, per-write `Target`s in the `Modify` stage. Four forces shaped this decision:

- Clip blending samples every bone of every layer each frame, which is costly as per-write modifier targets.
- Game code must stay able to build its own state machine by writing plain components.
- Partial-body clips should be usable without a mask for every case.
- Events from clips that are not visibly contributing must not trigger gameplay effects.

FenrirEngine therefore blends clips inside FenrirAnimation's `Animate` work, as part of producing the base pose. Layers are entities, crossfades are implicit on a clip change, and masks are shared assets.

## Where blending runs

- **Clip blending produces the base pose.** FenrirAnimation's sampler in the `Animate` stage (ADR-0033) samples, fades and layers every clip of a skeleton and writes the result through `EditBasePose`. The base pose stays "what the clips say".
- **Modifiers are procedural.** IK, procedural posing, ragdolls and gameplay adjustments stay pose modifiers in `Modify`, on the final pose. Layers and masks are not modifiers.
- **In scope:** crossfades, weighted override layers, additive layers and bone masks.
- **Not in this spec:** blend trees, blend spaces, animation state machines and sync groups stay in *Richer animation*. A state machine can be built in game code that writes `Animator` and `AnimationLayer`.

## Layers

- **`Animator` is the base layer.** It stays on the skeleton entity (ADR-0034) and gains the fade fields below. It has no weight, mask or mode: it is always a full override at the bottom, and it always evaluates first.
- **Every other layer is an entity** carrying a plain, blittable `AnimationLayer`:

  ```cpp
  struct AnimationLayer
  {
      Entity skeleton;                 // the skeleton entity it layers onto; need not be its ECS parent
      int order = 0;                   // after the base Animator, by order, then by entity
      float weight = 1.0f;             // clamped to [0, 1]
      LayerMode mode = LayerMode::Override; // Override or Additive
      AssetHandle<BoneMask> mask;      // empty: every bone at 1
      AdditiveReference reference = AdditiveReference::ClipStart; // ClipStart or RestPose; Additive only
      AssetHandle<ClipData> clip;
      float speed = 1.0f;
      ClipLoop loop = ClipLoop::Loop;
      bool playing = true;
      float fadeSeconds = 0.0f;
      FadeCurve fadeCurve = FadeCurve::Smooth;
  };
  ```

  Game code writes every field, and FenrirAnimation writes none.
- **Playback state is private.** Each layer's state lives in `Animators` (ADR-0034), keyed by the layer entity: its cursors, its fade, any snapshot, its bindings and its event cursors.
- **The entity is the handle.** `Animation::Seek(entity, seconds)` and `Animation::Playback(entity)` take the skeleton entity for the base `Animator`, or the layer entity for an `AnimationLayer`.
- **A layer can exist without a base `Animator`.** It then layers over the skeleton's retained base pose, whether that pose is hand-authored or last sampled. That foundation is never reset to rest each frame.

## Composition

- **The layers compose in sequence onto an accumulated local pose.** It starts as the base layer's result. Each later layer, in order, blends into it, with an effective per-bone weight of `weight × mask weight(bone)`. Nothing is normalised. A layer below weight 1 shows more of what lies beneath it.
- **Base layer.** It resolves channels its clip does not animate to the skeleton's rest locals, as ADR-0034 says. With no clip, it leaves the base pose alone.
- **Override layers pass through what they do not animate.** A channel the layer's clip does not animate is left at the accumulated value. An upper-body clip with no leg channels does not touch the legs, with or without a mask.
- **Additive layers.** A channel the clip does not animate is an identity delta.
- **Blend maths.** All blending is in local space, per channel (translation, rotation, scale), matching ADR-0033's `Target` convention. For effective weight `w`:
  - **Override:**
    - translation and scale: `lerp(current, sample, w)`;
    - rotation: `nlerp(current, sample, w)` along the shortest arc.
  - **Additive:**
    - translation: `current + (sample − reference) × w`;
    - rotation: `delta = sample × inverse(reference)`, then `result = slerp(identity, delta, w) × current`. The delta is applied before the accumulated local rotation. Tests use non-commuting rotations to pin the order.
    - scale: a per-component ratio `sample / reference`, blended from 1 as `lerp(1, ratio, w)` and multiplied into `current`.
- **Additive scale supports positive ratios only.** A scale component is degenerate when the reference or the sample is non-finite, or `|reference| < 1e-6`, or the ratio is not positive. A degenerate component gets an identity delta of 1, and a diagnostic names the clip and bone, once per binding. Mirrored or signed-scale additive animation is future work.

## Additive reference poses

- **Two modes:**
  - `ClipStart` (the default): the layer clip's own pose at time 0.
  - `RestPose`: the skeleton's rest locals.
- **A reference is a complete local pose.** Channels the clip does not animate take rest values.
- **Deltas are computed at run time from raw keys.** No asset is baked.
- **Reference poses are cached** per (clip handle and revision, skeleton handle and revision, reference mode), and dropped when unused, like bindings.
- **Later:** another clip as the reference waits for import options.

## Crossfades

- **A clip change fades.** When `clip` changes on an `Animator` or `AnimationLayer` whose `fadeSeconds` is above 0, the layer fades from its outgoing source to the new clip over `fadeSeconds`. At 0 it cuts, as ADR-0034 does.
- **The outgoing clip keeps playing.** During a normal fade, it advances on its own private cursor, independently of the incoming clip, which starts as ADR-0034 says: at 0, or at the duration in reverse.
- **Interruption snapshots.** A clip change during a fade freezes the layer's current blended pose as a snapshot, with the coverage bits of the channels it covered. A new fade then runs from the snapshot to the newer clip, or cuts if `fadeSeconds` is 0. There is no transition stack. Richer interruptible transitions are future work.
- **Fade clock.** Fade progress advances by `Time::deltaTime`, unscaled by `speed`, so a change of speed never changes a fade's duration. It freezes while the layer is paused, as its cursors do. Progress is clamped to [0, 1].
- **Fade curve.** The blend weight is `FadeCurve` applied to progress: `Linear` is the identity, and `Smooth` (the default) is smoothstep, `t·t·(3 − 2t)`.
- **Changes to an active fade.** A change to `fadeSeconds` during a fade does not alter it. It applies to the next clip change.
- **Coverage-aware fades.** For each bone channel of a fading layer:
  - `underlying` is the accumulated pose below the layer, or rest for the base layer;
  - `out` is the outgoing clip's (or snapshot's) value if it covers the channel, else `underlying`;
  - `in` is the incoming clip's value if it animates the channel, else `underlying`;
  - the layer's value is `blend(out, in, curve(progress))`, which is then applied over `underlying` with the layer's effective weight;
  - a channel covered by neither passes through untouched.

  So a channel appearing during a fade blends in from the underlying value, and one disappearing blends back to it.
  - For additive layers the same rule runs on deltas: a missing channel is the identity delta, and the outgoing and incoming deltas are blended before being applied.
- **Fading to and from no clip, on an `AnimationLayer`:**
  - empty → clip fades the layer's contribution in from `underlying`;
  - clip → empty fades it out, with the outgoing clip still advancing during the fade;
  - an interrupted fade snapshots as above;
  - a completed fade to empty clears the layer's runtime contribution, but never removes its entity or component.
- **Fading to and from no clip, on the base `Animator`:**
  - clip → empty leaves the base pose as last sampled (ADR-0034), with no fade;
  - empty → clip fades from the retained base pose, hand-authored or last sampled, to the new clip.
- **Seeking during a fade** moves only the incoming (current) clip's cursor. The outgoing clip or snapshot is untouched, and the fade continues. ADR-0034's precedence holds: a clip change applies first and the latest pending seek after it.
- **Completion.** `finished` and `ClipFinished` describe only the layer's current clip. An outgoing `Once` clip that reaches its end during a fade holds its last pose and sends nothing.

## Weight, playback and cost

- **Weight never touches the clock.** A layer's cursors and fade progress advance according to `playing` and `speed` alone. A weight of 0, including from the mask, only removes its contribution to the pose.
- **Skipped work.** FenrirAnimation skips sampling for a layer at weight 0, and for bones whose mask weight is 0. Cursors, fade progress and event detection still run.
- **No stale state.** A layer whose sampling was skipped samples on demand whenever a snapshot is needed (an interrupted fade) and when its weight rises above 0 again. It never reuses a pose or binding older than the current revisions.

## Bone masks

- **`BoneMask` is a plain-data asset** in FenrirAssets, shared and edited with a revision bump. It holds an ordered list of `{ boneName, weight, scope }` entries, where `scope` is `Bone` or `Subtree`, built in code with helpers such as `Include("spine_01", 1.0f, MaskScope::Subtree)`. Later entries override earlier ones, unlisted bones get 0, and an empty mask handle means every bone at 1.
- **Masks bind by name.** A mask resolves per skeleton into indexed per-bone weights, cached per (mask handle and revision, skeleton handle and revision), and dropped when unused.
- **Name matching follows ADR-0031.** A mask names bones by engine name, the reusable name, not source identity. A name that matches no bone, or matches ambiguously, is diagnosed once per binding and selects nothing. It never selects an arbitrary bone.

## Events

- **Contributing weight gates events.** A clip's `AnimationEvent`s fire only while it contributes: the layer's `weight` must be above 0, and the clip must be within its fade's eligible span. The mask is ignored, being per bone. Crossings skipped while silent are never replayed.
- **Fade eligibility is half-open:**
  - an incoming clip is eligible from the start of its fade, inclusive, so a clip faded in at time 0 still fires its event at 0;
  - an outgoing clip is eligible until its fade completes, exclusive;
  - a snapshot has no events;
  - an interrupted fade replays nothing.
- **Eligibility at the crossing instant.** Eligibility is judged at the crossing, not at the frame's start or end. A crossing's **frame fraction** `f` in [0, 1] is its distance along the clip's step this frame (whole laps included) divided by the step's length. The fade's progress at `f` decides it.
- **One chronological stream per skeleton.** Eligible crossings from every layer are ordered by frame fraction, with layer order then event index breaking ties. ADR-0034's budget, `maxEventsPerInstancePerFrame` (64), stays per skeleton instance per frame, shared by its layers, and keeps the earliest 64 in that order.
  - Crossings past the budget are counted arithmetically per layer (whole laps times eligible events per lap, plus the partial laps), never enumerated.
  - They are reported through `AnimationEventsDropped`, as ADR-0034 says.
- **Layer field on events.** `AnimationEvent` and `ClipFinished` gain `Entity layer`: the layer entity, or the skeleton entity for the base `Animator`. `ClipFinished` is state, not effect, so it is sent whatever the layer's weight.

## Playback and lifecycle

- **`Animation::Playback(entity)`** gains `fading` (bool) and `fadeProgress`, the normalised, curve-free progress in [0, 1]. The blend weight is `fadeCurve` applied to it. `time`, `completedLoops` and `finished` stay those of the current clip, and the outgoing clip's time is not exposed.
- **Unavailable skeletons.** A layer whose `skeleton` is missing, is not a skeleton entity or is not `Ready` contributes nothing, and its cursors and fade do not advance. `Playback` reports `Missing` or `Unavailable`.
- **Retargeting.** Changing `skeleton` restarts the layer as a clip change does, with no fade across skeleton instances.
- **Removal.** Destroying a layer entity, or removing its `AnimationLayer`, cuts its contribution at once. Its state is released at the next reconcile.
- **Configuration changes.** Changes to `order`, `weight`, `mode`, `mask` and `reference` take effect at the next evaluation.

## Tests

Headless, on procedural skeletons, clips and masks:

- **Composition:**
  - override layers with weights and masks, including subtree masks and later entries overriding earlier ones;
  - pass-through of channels a layer does not animate;
  - a layer with no base `Animator` over a hand-authored base pose.
- **Additive maths:**
  - against the formulas, with non-commuting rotations;
  - both reference modes;
  - degenerate scale components with their diagnostics.
- **Crossfades:**
  - both curves;
  - speed-independent duration, and pausing mid-fade;
  - an interrupted fade (continuous at the interruption, then fading from the snapshot);
  - channel coverage appearing and disappearing;
  - fades to and from empty on layers and on the base;
  - seeks during a fade;
  - an outgoing `Once` clip ending mid-fade.
- **Events:**
  - zero-weight layers silent while their cursors run;
  - half-open eligibility at the fade's ends, including an event at 0 on a faded-in clip;
  - chronological order across layers with tie-breaks;
  - the shared budget and arithmetic drop counts across layers;
  - `layer` on events.
- **Lifecycle:**
  - missing and unready skeletons;
  - retargeting;
  - removal;
  - weight returning above 0 after skipped sampling;
  - cache invalidation of masks and reference poses on revision changes.

## Considered options

- **Layers and masks as pose modifiers.** Rejected: every layer would pay a per-bone `Target` resolve, and clip blending would mix with procedural work, which ADR-0034 kept out of the base pose.
- **A fixed array of layer slots in `Animator`.** Rejected: a hard cap, and a fixed buffer for C# to mirror.
- **An explicit `CrossFade` call.** Rejected: a command rather than plain data, invisible to C# mirrors and inspectors.
- **Normalising layer weights, or falling back to rest below a total of 1** (ozz). Rejected: sequential composition gives a layer below weight 1 a clear meaning without either.
- **Override layers resolving missing channels to rest.** Rejected: partial-body clips would pull unanimated bones to rest unless masked.
- **A transition stack for interrupted fades.** Deferred: a snapshot avoids a visible jump, at the cost of freezing the interrupted blend.
- **Events from every layer whatever its weight.** Rejected: a silent upper-body layer would trigger gameplay effects.
- **Events delivered layer by layer.** Rejected: chronological order across layers is what gameplay sees.
- **Baked additive deltas.** Rejected for now: run-time deltas from raw keys need no new asset kind.

## Consequences

- *Amends ADR-0028:* layers and masks are base-pose blending in FenrirAnimation, not pose modifiers; modifiers are procedural and physics work.
- *Amends ADR-0033:* its list of later modifiers no longer includes layers and masks.
- *Amends ADR-0034:*
  - more than one clip per skeleton, through layer entities and crossfades;
  - `AnimationEvent` and `ClipFinished` gain `layer`;
  - the event budget is shared by a skeleton's layers in chronological order;
  - `Playback` gains `fading` and `fadeProgress`.
- Ticket 33 gives C# `AnimationLayer`, `BoneMask` and the new fields, and ticket 34's demo can show layers, masks and fades.
- Not yet specified (*Richer animation*):
  - blend trees, blend spaces, state machines and sync groups;
  - interruptible transitions beyond a snapshot;
  - another clip as additive reference;
  - signed-scale additive animation.
