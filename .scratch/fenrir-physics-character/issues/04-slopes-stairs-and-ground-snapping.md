# 04: Slopes, stairs and sticking to the ground

**What to build:** A character walks a level without tuning: up a ramp but not a cliff, up and down stairs without ever becoming airborne, sticking to the ground going down, and standing still on walkable slopes unless told to slide (spec: "the move in `PostTick`" (slopes, stairs, ground sticking, the trim rule), "ground state"; spike lessons 1 and 2).

- **Live fields wired up:** `maxSlope` (velocity into a too-steep slope cancelled), `stepHeight` (stairs), `floorSnap` (sticking to the ground walking down) and `stopOnSlope`, each switched off by 0 (or `false`), each applied at the next move. Finer stair tuning stays private.
- **The full trim rule on slopes:** on walkable ground with `stopOnSlope` an up-facing contact takes all of the falling part; otherwise only its component into the surface, so the rest slides the character downhill. Walking velocity is never trimmed along an up-facing contact's normal (that turned walking into climbing in the spike). Stair step-ups and ground snaps never show in `velocity`.
- **All four ground states:** `OnSteepGround` (steeper than `maxSlope` underneath) with the ground fields set, and `Touching` (something touched, nothing underneath) with them empty.
- **Rounded edges:** a ground contact's steepness is judged by the surface normal of the face it touches, not the contact normal, so a box's rounded edge over a walkable face is walkable ground; `groundNormal` reports that normal. If Jolt's ground state cannot be fed the face normal, say so in this ticket and ask before falling back to accepting the flicker.
- **DemoApp:** the 30° ramp, the 55° slope, ten 0.3 m stairs to a 3 m ledge; key 1 toggles `stopOnSlope`, 5 toggles stairs and ground snapping; the autopilot climbs the stairs and walks the ramp.

**Blocked by:** 03 (Character tracer bullet).

**Status:** ready-for-agent

- [ ] Tests: on a 30° slope the character walks up and, with `stopOnSlope`, holds still within 1 mm over 2 s; with it off it slides, and adding input to the written-back velocity keeps the slide's speed.
- [ ] Tests: a 60° slope is not climbed and reads `OnSteepGround`; 0.3 m stairs are climbed with no airborne tick and no upward speed in `velocity`, and descended with no `OnSteepGround` tick.
- [ ] Tests: `maxSlope`, `stepHeight` and `floorSnap` each switched off by 0 change behaviour on the next tick; `Touching` against a ledge with nothing underneath; `movedVelocity` includes stair step-ups.
- [ ] The autopilot route with 1 and 5 runs with no errors under `FENRIR_DEMO_KEYS=1`, in Debug and Release.
- [ ] The ramp is walked, the 55° slope not climbed and reads "on steep ground", 1 off gives a full-speed slide, stairs climb with no airborne ticks and descend without flicker, and 5 off blocks them (checked by hand).
