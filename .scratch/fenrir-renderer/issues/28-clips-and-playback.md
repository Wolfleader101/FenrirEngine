# How are clips played?

Type: grilling
Status: resolved
Blocked by: 26

## Question

FenrirAnimation samples clips into a skeleton's base pose (ADR-0028). What does game code put on an entity to play one, and how does playback behave?

- The component (an `Animator`, a player, or other) and which entity carries it: the skeleton entity, or one that names it.
- Binding: clips name bones by name, resolved to indices once per clip and skeleton and cached; bones a clip lacks (rest pose), clip channels with no matching bone, and non-bone nodes a clip animates.
- Sampling: interpolation between keys (linear, step, cubic from glTF), quaternion handling, what ADR-0021's importer already normalises.
- Playback state: time, speed, looping, clamping, pausing; what time it advances by (frame delta in `PostUpdate`).
- Events in clips and gameplay synchronisation: how an event reaches game code (an ECS event, read once per reader as ADR-0012), events crossed by a loop or a large step.
- Root motion: in this spec or deferred.

## Comments

- From ticket 26 (ADR-0031): each clip channel keeps its target's source node identity and engine name; a clip binds to its own file's skeletons by identity and to others by engine name, with diagnostics for missing or ambiguous matches; channels on ordinary (non-bone) nodes are kept, and how they drive entities is this ticket's.
- From ticket 27 (ADR-0033): FenrirAnimation adds its sampler to FenrirSkeleton's public `Animate` stage with an order constant; it writes only the base pose. Its clocks advance only by the frame delta of an `Animate` run on their scene, so an inactive scene's playback pauses and resumes with no catch-up. Playback commands are written by game code before the pipeline (`Update` or earlier).

## Answer

ADR: `docs/adr/0034-clips-play-through-a-game-owned-animator-and-fenriranimation-owns-playback-time-and-events.md`. Glossary: Animator, Playback cursor, Animation event; Clip revised.

- **Component:** `Animator { AssetHandle<ClipData> clip; float speed = 1; ClipLoop loop = Loop; bool playing = true; }` on the skeleton entity, game-owned, plain and C#-mirrorable; FenrirAnimation never writes it. The playback cursor (`double` seconds), loops, `finished`, pending seeks, key and event cursors and bindings live in FenrirAnimation's private `Animators` scene resource, reconciled as `SkeletonPoses` is. One clip per `Animator`; ticket 29 decides how it grows past one.
- **Base pose:** while `clip` is non-empty FenrirAnimation owns the whole base pose, starting each pass from rest locals and applying T, R and S channels independently (missing ones keep rest); with no `Animator` or an empty clip, hand-edited base poses persist; procedural changes are modifiers. Runs in ticket 27's base-pose stage, in `PostUpdate` for now; **depends on ticket 27** for that stage's name and order.
- **Playback:** advances by `Time::deltaTime × speed` (negative reverses); `ClipLoop::Loop` and `Once` (clamps and holds); `finished` and one `ClipFinished` per false-to-true, reset by a clip change, a seek or a direction reversal away from the boundary, kept by pausing; `completedLoops` `uint64_t`, saturating; `playing = false` freezes the cursor but keeps sampling; clip changes restart at 0 (or the duration in reverse); `Animation::Seek(entity, seconds)` queued, applied after any clip change, latest wins, works while paused; `Animation::Playback(entity)` reads state, time, loops and `finished`; zero-duration clips hold, never wrap, finish once.
- **Sampling:** raw keys at run time; glTF `STEP`, `LINEAR` (shortest-path slerp for rotation) and `CUBICSPLINE` (Hermite with interval-scaled tangents, rotation normalised after, tangents never normalised); the importer makes rotation keys hemisphere-continuous, flipping tangents with them; FBX baked through ufbx's baking API at a documented internal rate; authored key times kept, playback from 0 to the last key with the first key held until its time; per-channel cursors falling back to a search after seeks, reversal or wraps.
- **Bindings:** cached per (clip handle and revision, skeleton handle and revision), shared, numeric indices only; identity within a file, engine name otherwise (ADR-0031); rebuilt on revision change, dropped when unused; one diagnostic per channel naming target, path and reason; ordinary-node channels kept in `ClipData`, not played (node animation deferred).
- **Unavailable:** a skeleton not `Ready` is neither sampled nor advanced; a stale non-empty clip freezes the cursor and pose, reports `Unavailable`, and resumes without catch-up events, the cursor clamped (`Once`) or wrapped (`Loop`) into a changed revision's duration; the renderer skips draws only for unavailable skeletons.
- **Events:** code-authored `{ time, name }` in `ClipData`, sorted by time with ties in authored order; identity is index plus clip revision, the FNV-1a `ClipEventId` only a lookup; `AnimationEvent { skeleton, clip, clipRevision, index, nameId, time, loop }` via ADR-0012's queues from the base-pose stage; (prev, cur] forward, [cur, prev) reverse, in playback order, the starting boundary included, wraps across every lap; seeks, clip changes, pauses, zero speed and recovery cross nothing; at most `AnimationConfig::maxEventsPerInstancePerFrame` (64) sent, the earliest, the rest counted arithmetically into one `AnimationEventsDropped { skeleton, clip, uint64_t dropped }` (saturating) and logged once; best-effort, frame-based.
- **Root motion:** deferred; the root bone is sampled like any other, so baked locomotion moves the skeleton relative to its entity.
- **Tests:** the interval table (forward from 0, across a wrap, reverse across 0, seek, pause, zero duration, 100 laps against the budget), seek and clip-change precedence, completion resets, glTF sampling formulas and hemisphere flips, clip loss and recovery, revision changes and binding diagnostics.
- **Graduated:** nothing new; comments on tickets 29, 33 and 34; *Richer animation* gains node animation, ping-pong, importer-sourced clip events and fixed-tick animation for guaranteed events.
