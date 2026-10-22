# How are poses blended?

Type: grilling
Status: resolved
Blocked by: 27, 28

## Question

How much blending does the first animation spec have, and how does it fit ADR-0028's base pose and ordered modifiers?

- Crossfades between clips: who starts one, over what time, and with what curve.
- Layers: weighted and additive, in order; masks by bone (by name, by subtree, per-bone weights).
- Whether layers and masks belong to FenrirAnimation's base pose or become pose modifiers (ADR-0028 lists layers and masks among modifiers to come).
- Whether a blend tree or graph (blend spaces, state machines) is in this spec or stays in *Richer animation*.
- Weights that sum below one: fall back to the rest pose, as ozz does, or normalise.

## Answer

ADR: `docs/adr/0036-clips-blend-in-fenriranimation-through-layer-entities-crossfades-and-bone-masks.md`, amending ADR-0028, ADR-0033 and ADR-0034. Glossary: Animation layer, Crossfade, Bone mask, Additive layer, Reference pose; Animator revised.

- **Scope:** crossfades, weighted override layers, additive layers and bone masks. Blend trees, blend spaces, state machines and sync groups stay in *Richer animation*; game code builds a state machine by writing `Animator` and `AnimationLayer`.
- **Where:** clip blending is FenrirAnimation's base-pose work in `Animate`, written through `EditBasePose`; layers and masks are not pose modifiers (ADR-0028 and ADR-0033 amended), which stay procedural and physics work in `Modify`.
- **Layers:** `Animator` on the skeleton entity is the base layer, always first, a full override with no weight, mask or mode. Other layers are entities with a blittable `AnimationLayer { skeleton, order, weight, mode (Override/Additive), mask, reference (ClipStart/RestPose), clip, speed, loop, playing, fadeSeconds, fadeCurve }`, ordered by `order` then entity; `skeleton` need not be the ECS parent. Playback state stays private in `Animators`, keyed by the layer entity; `Seek` and `Playback` take the skeleton entity or the layer entity. A layer without a base `Animator` layers over the retained base pose, never reset to rest.
- **Composition:** sequential, no normalisation; effective weight is `weight × mask(bone)`, clamped to [0,1]. The base layer resolves unanimated channels to rest; override layers pass unanimated channels through; additive layers treat them as identity deltas. Override: lerp translation and scale, shortest-arc nlerp rotation. Additive: `current + (sample − reference) × w`; `slerp(identity, sample × inverse(reference), w) × current`; scale ratio blended from 1, positive ratios only, a degenerate component (non-finite, `|reference| < 1e-6`, non-positive ratio) taking 1 with a once-per-binding diagnostic.
- **Reference poses:** a complete local pose (rest for unanimated channels) from the clip at 0 (the default) or the rest pose, computed at run time from raw keys, cached per (clip, skeleton, mode) revisions; another clip as reference later.
- **Crossfades:** a clip change with `fadeSeconds > 0` fades; 0 cuts. The outgoing clip keeps advancing on its own cursor; an interrupted fade freezes the layer's blended pose as a snapshot (with coverage bits) and fades from it, with no transition stack. Progress advances by unscaled `Time::deltaTime`, freezes on pause, is clamped to [0,1], and goes through `FadeCurve` `Linear` or `Smooth` (smoothstep, the default); a `fadeSeconds` change leaves a running fade alone. Per channel, `out` and `in` fall back to the underlying accumulated value (rest for the base) where a clip does not cover it, so channels appear and disappear smoothly; additive layers do the same on deltas. On layers, empty → clip fades in, and clip → empty fades out (the outgoing clip still advancing) and then clears the contribution, keeping the entity; on the base, clip → empty keeps the last pose and empty → clip fades from the retained base pose. A seek moves only the incoming cursor; ADR-0034's clip-change-then-seek order holds. Only the current clip reports `finished` and sends `ClipFinished`.
- **Weight and cost:** weight never touches cursors or fades. Sampling is skipped for zero-weight layers and zero-mask bones while clocks and event detection run; a skipped layer samples on demand for a snapshot and when its weight returns, never reusing stale poses or bindings.
- **Masks:** a `BoneMask` asset in FenrirAssets: ordered `{ boneName, weight, Bone/Subtree }` entries, later overriding earlier, unlisted bones 0, an empty handle all 1; bound by engine name per skeleton, cached per (mask, skeleton) revisions; unmatched or ambiguous names diagnosed once and select nothing.
- **Events:** a clip's events fire only while the layer's `weight > 0` and the clip is fade-eligible: incoming from its fade start inclusive (so time-0 events still fire), outgoing until its fade ends exclusive, snapshots never; masks ignored; silent crossings never replayed. Crossings carry a frame fraction and merge across layers chronologically (ties by layer order, then event index) into ADR-0034's per-skeleton budget of 64, overflow counted arithmetically per layer into `AnimationEventsDropped`. `AnimationEvent` and `ClipFinished` gain `Entity layer` (the skeleton entity for the base); `ClipFinished` fires whatever the weight.
- **Playback and lifecycle:** `Playback` gains `fading` and the curve-free `fadeProgress`; other fields describe the current clip. A layer on a missing, non-skeleton or unready entity contributes nothing and does not advance (`Missing`/`Unavailable`); changing `skeleton` restarts with no fade; removing a layer cuts it and frees its state at reconcile; other fields apply at the next evaluation.
- **Tests:** composition with masks and pass-through, additive maths with non-commuting rotations and degenerate scales, both reference modes, fades (curves, speed independence, pause, interruption, coverage, empty clips, seeks, an outgoing `Once`), events (zero weight, half-open eligibility, chronological merge, shared budget, `layer`), and lifecycle and cache invalidation.
- **Graduated:** nothing new; comments on tickets 33 and 34; *Richer animation* gains blend spaces, sync groups, interruptible transitions beyond a snapshot, another clip as additive reference and signed-scale additive animation.

## Comments

- From ticket 27 (ADR-0033): a modifier is `Skeleton::AddModifier(name, order, fn)` writing `Target`s (local/model/world, channel mask, weight) through `PoseEdit`, composed sequentially in `Modify`; layers and masks may be base-pose blending inside `Animate` or modifiers on this interface.

- From ticket 28 (ADR-0034): `Animator` on the skeleton entity is game-owned configuration for one clip (clip, speed, loop, playing), with playback state in FenrirAnimation's private `Animators`; this ticket decides how it grows to crossfades, layers and several clips (slots, layer components or entities), keeping time out of components and explicit seeking.
