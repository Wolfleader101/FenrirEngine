---
status: accepted
---

# Clips play through a game-owned Animator, and FenrirAnimation owns playback time and events

ADR-0028 made FenrirAnimation the package that samples clips into a skeleton's base pose, and ADR-0031 made `SkeletonPose` a plain component whose arrays a scene resource owns. Renderer map ticket 28 decided how game code plays a clip. Three forces shaped it. First, components must be plain and blittable so C# can mirror them. Second, a time field that both game code and the animation system write has no clear owner: a write could be a seek, or just the system's own last value. Third, render-frame playback can cross any number of clip events in one hitch, and nothing should turn one frame into an unbounded flood of ECS events. FenrirEngine therefore splits playback into game-owned configuration in a component and system-owned playback state in a scene resource, mirroring `SkeletonPose` and `SkeletonPoses`. Seeking is explicit, and event delivery is exact in detection but bounded in emission.

## Animator and Animators

- **`Animator` is game-owned configuration** on the skeleton entity, one per skeleton:

  ```cpp
  struct Animator
  {
      AssetHandle<ClipData> clip;
      float speed = 1.0f;
      ClipLoop loop = ClipLoop::Loop;
      bool playing = true;
  };
  ```

  It is plain, blittable and mirrored in C# (ticket 33). Game code writes every field, and FenrirAnimation never writes any of them. It holds no time.
- **`Animators`, a FenrirAnimation scene resource, owns playback state**: the playback cursor (a `double`, in seconds), completed loops, `finished`, pending seeks, per-channel key cursors, event cursors and the binding cache. Its storage is private. It is reconciled against the components each frame, as `SkeletonPoses` is (ADR-0031).
- **One playing clip per `Animator`.** Crossfades, layers and several simultaneous clips are ticket 29's, which decides how this model grows past one clip.
- **Root motion is not extracted.** The root bone is sampled like any other bone, so locomotion in a root-bone channel moves the skeleton relative to its skeleton entity, not the entity itself. Extraction can later give an entity-space delta without changing the pose model.

## Ownership of the base pose

- **While an `Animator` names a non-empty clip, FenrirAnimation owns the whole base pose.** Each sampling pass starts from the skeleton's rest locals and applies the clip's translation, rotation and scale channels independently, through `EditBasePose`. A bone or channel the clip lacks keeps its rest value.
- **With no `Animator`, or an empty `clip`, the base pose is left alone**, so a pose edited by hand persists.
- **Procedural changes to an animated skeleton are pose modifiers** (ticket 27), never base-pose writes.
- **Sampling runs in ticket 27's base-pose stage**, in `PostUpdate` for now (ADR-0028). Ticket 27 names that stage and orders it.

## Playback

- **Time advances by `Time::deltaTime × speed`** (FenrirTime) once per frame in the base-pose stage. Negative speed plays in reverse, and a speed of 0 holds. There is no global time scale or pause yet.
- **`ClipLoop` has two modes:**
  - `Loop` wraps, however many laps one step covers.
  - `Once` clamps at the end, or at 0 in reverse, and holds that pose.
- **Completion.**
  - `finished` becomes true when a `Once` clip reaches its boundary, and one `ClipFinished { Entity skeleton; AssetHandle<ClipData> clip; }` event is sent per false-to-true transition.
  - It resets on a clip change, on a seek, and when a finished `Once` clip's speed changes sign so that it plays away from its boundary.
  - Pausing never resets it.
- **`completedLoops` is a `uint64_t`** that saturates, counting wraps in either direction.
- **Pausing.** `playing = false` freezes the cursor and keeps sampling, so FenrirAnimation still owns the base pose, held at the current time.
- **Clip changes restart deterministically:** at 0 for a positive speed, at the duration for a negative one.
- **Seeking is explicit.** `Animation::Seek(entity, seconds)` queues a seek, applied at the next evaluation, so a seek may precede the instance's creation.
  - A clip change is applied first and the latest pending seek after it, whichever order game code issued them in, so `clip = run; Seek(e, 0.5)` starts `run` at 0.5.
  - A seek resets `finished`. A seek while paused moves the cursor and the pose at the next evaluation without resuming playback.
- **`Animation::Playback(entity)`** reads `{ state, time, completedLoops, finished }`, where `state` is `Ready`, `Unavailable` or `Missing`, as for skeleton instances.
- **Zero-duration clips** (no key after time 0):
  - they sample their first keys, and the cursor stays at 0;
  - `Loop` never wraps;
  - `Once` finishes on its first evaluation, once until restarted;
  - events at 0 fire once when playback starts, within the event budget.

## Sampling

- **Raw keys, sampled at run time.** Nothing is resampled at import except FBX.
- **Interpolation per channel, as glTF defines it:**
  - `STEP` holds a key;
  - `LINEAR` lerps translation and scale, and slerps rotation along the shortest path;
  - `CUBICSPLINE` is glTF's Hermite form, with tangents scaled by the key interval, and rotation results normalised after interpolation. Tangents are never normalised as quaternions are.
- **Hemisphere continuity.** The importer, which already makes quaternions unit (ADR-0021), flips any rotation key whose dot product with the previous key is negative, and flips that key's in- and out-tangents with it.
- **FBX** clips are baked to linear keys through ufbx's animation-baking API, at a documented internal sample rate. Import options may expose it later.
- **Clip time.** A clip keeps its keys' authored timestamps. Playback time runs from 0 to the last key's time, the clip's duration, even if the first key is later; before the first key its value holds, and after the last key that key's value holds.
- **Key cursors.** Each channel keeps a cursor so forward playback does not search keys. After a seek, reverse playback or a wrap breaks the sequential assumption, it falls back to a search.

## Bindings

- **A binding maps a clip's channels to bone indices** once per pair (the clip's handle and revision, the skeleton's handle and revision), cached in `Animators` and shared by every instance of the pair. Sampling uses only numeric indices, with no string lookups.
- **Matching (ADR-0031).** A clip binds to skeletons from its own model file by source identity, and to others by exact engine name.
- **Rebinding and cleanup.** A binding is rebuilt when either revision changes, including on asset replacement, and dropped at the first reconcile at which no instance uses its pair.
- **Diagnostics** are logged once per binding. Each names the clip, the channel (target name and path: translation, rotation or scale) and the reason:
  - no bone matches;
  - the match is ambiguous;
  - the target is an ordinary node, whose channel is kept in `ClipData` but not played. Node animation of entity transforms is future work.

## Unavailable clips and skeletons

- **A skeleton that is not `Ready`** is not sampled, and its cursor does not advance.
- **An empty clip handle** means no active animation (see *Ownership of the base pose*).
- **A non-empty handle that is stale or unavailable** suspends playback:
  - the cursor freezes and the base pose keeps its last sampled values;
  - `Playback` reports `Unavailable`, and the problem is logged once.
- **On recovery** the binding is revalidated. If the revision changed, the cursor is clamped into the new duration for `Once` or wrapped into it for `Loop`. No events fire for the suspended interval or the adjustment.
- **The renderer is unaffected by clip availability.** It skips skinned draws only for an unavailable skeleton (ADR-0031), and otherwise draws the last valid pose.

## Clip events

- **Authored from code.** glTF and FBX have no standard for clip events, so they are added by editing `ClipData` through FenrirAssets, which bumps its revision. Importer sources (glTF `extras`, FBX markers) wait for import options.
- **`ClipData` keeps a list of `{ float time; std::string name; }`,** sorted by time, with equal times keeping authored order. An event's **index** in that list, together with the clip's revision, is its identity. A name lookup returns every index with that name.
- **Delivery is an ECS event** (ADR-0012), sent from the base-pose stage:

  ```cpp
  struct AnimationEvent
  {
      Entity skeleton;
      AssetHandle<ClipData> clip;
      uint32_t clipRevision;
      uint32_t index;
      ClipEventId nameId; // 32-bit FNV-1a of the name, a lookup convenience only
      float time;         // the authored time
      uint64_t loop;      // the lap the crossing happened in
  };
  ```

  An index is valid only for its `clipRevision`. `PostUpdate` readers ordered after the base-pose stage see an event in the same frame, and `Update` readers see it the next frame.
- **Crossing rules.**
  - Forward playback crosses events in (previous, current], and reverse playback crosses events in [current, previous). Events are delivered in playback order.
  - When playback starts forward at 0, an event at exactly 0 is included. When it starts in reverse at the duration, an event at exactly the duration is included.
  - A wrap crosses the rest of the old lap, every whole lap between, then the new lap's part.
  - A `Once` clip reaching its boundary crosses its boundary events once.
  - Seeks, clip changes, pauses, a speed of 0 and recovery from suspension cross nothing.
- **Bounded emission.** Playback always advances by the full step, and loop counts stay exact. Crossings are detected exactly, in playback order, but at most `AnimationConfig::maxEventsPerInstancePerFrame` (default 64) are sent per instance per frame: the earliest in playback order.
  - Past the budget, the remaining crossings are counted arithmetically (whole laps times events per lap, plus the partial laps), never iterated.
  - One `AnimationEventsDropped { Entity skeleton; AssetHandle<ClipData> clip; uint64_t dropped; }` event is sent, with `dropped` saturating, and the drop is logged once per instance.
- **These are best-effort, frame-based events.** Gameplay that needs guaranteed delivery waits for fixed-tick animation.

## Tests

Playback and event tests run headless, on procedural clips and skeletons. They cover:

- **Interval semantics**, as a table, with duration 1:
  - forward 0 → 0.5 fires the crossed events, including one at 0;
  - forward 0.8 → 1.2 fires the end of the first lap, then the start of the next, in order;
  - reverse 0.2 → −0.2 fires the crossed events in reverse order, wrapping;
  - a seek to 0.8 fires nothing;
  - a pause at 0.5 fires nothing and holds the pose;
  - a zero-duration clip holds a stable pose, never wraps, and its `Once` finishes once;
  - a step across 100 laps counts 100 loops exactly, sends at most the budget and reports the rest as dropped.
- **Seeking and precedence:** a clip change plus a seek in either order before one evaluation; a seek while paused; several pending seeks.
- **Completion:** `ClipFinished` once per transition, and reset on a clip change, a seek and a direction reversal.
- **Sampling:** `STEP`, `LINEAR` and `CUBICSPLINE` against glTF's formulas, including tangent scaling, hemisphere flips with their tangents, holding before the first key and after the last, and rest values for missing channels.
- **Assets:** a clip becoming unavailable and recovering with no catch-up events; a revision change rebinding and clamping or wrapping the cursor; binding diagnostics per channel; cache cleanup.

## Considered options

- **Time in the component, written by both game code and the system.** Rejected: whether a write is a seek or the system's own value is ambiguous, and so is which events a jump crosses.
- **An imperative API with a component that only names the instance.** Rejected: game code could not see or write what it asked for as plain data, unlike physics (ADR-0011).
- **One entity per playing clip.** Rejected for this ticket: playing a clip would mean spawning an entity. Ticket 29 may still use entities for layers.
- **A 32-bit name hash as event identity.** Rejected: hashes collide, and one name can occur at several times.
- **Unbounded event delivery.** Rejected: a hitch on a short or fast clip would flood the queues.
- **Resampling every clip at import.** Rejected for glTF, whose keys sample exactly; kept only for FBX, whose curve types do not map onto glTF's.
- **Animating ordinary nodes' entity transforms now.** Deferred: it is node animation, wider than skeletons.

## Consequences

- Ticket 29 extends `Animator` past one clip, ticket 33 gives C# `Animator`, `Seek`, `Playback` and the events, and ticket 34's demo shows events and the budget.
- *Amended by ADR-0036:* a skeleton plays more than one clip through `AnimationLayer` entities and crossfades on `Animator` and layers; `AnimationEvent` and `ClipFinished` gain `layer`; the event budget is shared by a skeleton's layers in chronological order; `Playback` gains `fading` and `fadeProgress`.
- *Amended by ADR-0040:* a tick-posed skeleton's playback advances once per fixed tick by `Time::tickRate`, not by the frame delta, and its events fire in the tick whose step crosses them.
- *Settled while building it (animation ticket 04):* `Animation::Seek` and `Animation::Playback` take the scene with the entity, as FenrirSkeleton's reads do, and `Animation::Install` takes the `Skeletons` owner whose `Animate` stage it joins. A clip knows a skeleton is from its own file through `AssetStore::FromSameFile`.
- Not yet specified: node animation of entity transforms, ping-pong looping, root-motion extraction, importer-sourced clip events, a global time scale, and fixed-tick animation for guaranteed events.
