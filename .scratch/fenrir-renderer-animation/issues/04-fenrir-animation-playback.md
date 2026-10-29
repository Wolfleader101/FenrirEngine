# 04: FenrirAnimation plays a clip through the Animator and delivers its events

**What to build:** the new FenrirAnimation package: writing an `Animator` on a skeleton entity plays its clip into the base pose each frame, with speed, reverse, pause, loop or once, seeks, playback state, and bounded clip events as ECS events, proven headlessly and by a bone check against analytic values (spec: "FenrirAnimation: playback"; ADR-0034). Blending (layers, crossfades, masks) is ticket 08 and tick-time playback ticket 11; this ticket's `Animator` is the base layer with `fadeSeconds` and `fadeCurve` present on the component but a clip change cutting until ticket 08.

- **Package:** `packages/FenrirAnimation/`, built under `FENRIR_ENABLE_ASSETS`, depending on FenrirSkeleton and FenrirAssets, on glm with no ozz. `Animation::Install(App&, AnimationConfig)` adds the sampler to `Animate` and returns the owner; it fails with one diagnostic without a valid `Skeletons` owner. `AnimationConfig::maxEventsPerInstancePerFrame` defaults to 64.
- **`Animator`** as the spec's struct, game-owned and holding no time. The private `Animators` scene resource, reconciled like `SkeletonPoses`, holds the `double` cursor, saturating `completedLoops`, `finished`, pending seeks, key and event cursors, and the binding cache.
- **Base pose ownership:** while the `Animator` names a clip, FenrirAnimation writes the whole base pose from rest locals each pass, channels applied independently; with no `Animator` or an empty clip the base pose is left alone.
- **Playback:** clock by step × `speed`, negative reversing, 0 holding; `Loop` wrapping any number of laps, `Once` clamping and holding; `finished` with one `ClipFinished` per false-to-true transition, reset on clip change, seek or reversal away from the boundary, kept by pausing; `playing = false` freezing the cursor but sampling; a clip change restarting at 0 or at the duration in reverse; zero-duration clips holding and finishing once. `Animation::Seek` queued, applied after any clip change, latest wins, working while paused and before the instance exists. `Animation::Playback` returning `PlaybackInfo` with `state`, `time`, `completedLoops`, `finished`, `fading` and `fadeProgress`.
- **Sampling:** `Step` holds, `Linear` lerps with shortest-path slerp, `CubicSpline` as glTF's Hermite form with interval-scaled tangents and rotations normalised after; holds before the first and after the last key; per-channel cursors falling back to a search after a seek, reversal or wrap.
- **Bindings:** cached per (clip handle and revision, skeleton handle and revision), matched by identity within the clip's own file else by exact engine name, rebuilt on revision change, dropped when unused, one diagnostic per unbound channel naming clip, target, path and reason.
- **Unavailable:** an unready skeleton neither sampled nor advanced; a stale non-empty clip freezing cursor and pose and reporting `Unavailable` once; recovery clamping or wrapping into a changed duration with no catch-up events.
- **Events:** `AnimationEvent` and `AnimationEventsDropped` as the spec's structs, sent from `Animate` through ADR-0012's queues; crossings forward `(prev, cur]` and reverse `[cur, prev)` in playback order, the starting boundary included when playback starts, a wrap crossing every lap; seeks, clip changes, pauses, zero speed and recovery crossing nothing; at most the budget per instance per frame, the rest counted arithmetically and the drop logged once per instance. No root-motion extraction.
- **Docs:** the package README, GLOSSARY entries (animator, playback cursor, animation event), the root `CMakeLists.txt` adding the package after FenrirSkeleton.

No demo scene yet: scene 1 (Playback) is built with AnimationDemo in ticket 05.

**Blocked by:** 03

**Touches:** `packages/FenrirAnimation/`, `CMakeLists.txt`, `GLOSSARY.md`

**Status:** ready-for-agent

- [ ] Seam 9 covers ADR-0034's interval table (forward from 0, across a wrap, reverse across 0, a seek, a pause, zero duration, 100 laps against the budget), seek and clip-change precedence, completion resets, and `Step`, `Linear` and `CubicSpline` against glTF's formulas with tangent scaling and hemisphere flips.
- [ ] Seam 9 covers clip loss and recovery with no catch-up, revision rebinding with clamp or wrap, binding diagnostics and cache cleanup, and install failing without FenrirSkeleton.
- [ ] The headless bone check: a small checked-in fixture (Khronos `RiggedSimple` or a hand-written rig) played to known times gives bone world transforms within tolerance of analytic values.
- [ ] FenrirAnimation links nothing from FenrirRenderer or FenrirPhysics and builds with `FENRIR_ENABLE_RENDERER=OFF`.
- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
