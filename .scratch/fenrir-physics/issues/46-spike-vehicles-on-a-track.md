# Spike: a car, a motorcycle and a tank driving a small track through the proposed model

Type: prototype
Status: resolved
Blocked by: 42, 43, 44

## Question

Do the vehicle model from [How is a vehicle modelled in the ECS?](42-vehicles-in-the-ecs.md), the settings from [Which settings does each vehicle kind expose?](43-vehicle-kinds-and-settings.md) and the per-tick step from [How does a vehicle step each tick, and what does game code read back?](44-how-a-vehicle-steps-and-reports.md) work in practice? Build a throwaway spike in the DemoApp on a `prototype/fenrir-physics-vehicles-spike` branch, in its own worktree: a car driven from the keyboard round a small track with a ramp, a bumpy section, a low-grip patch (a `PhysicsMaterial`), loose boxes to drive through, and a trailer on a `Joint`, plus a motorcycle and a tank to switch to; wheels drawn from their written-back poses; a follow camera. Made through the proposed public interface with Jolt behind the seam. Check it by hand in the DemoApp, as the earlier spikes were.

React to it: does the interface read well; does a car with default settings drive well; do motorcycles stay up and lean; do tanks turn on the spot; do wheels look right (spin, steer, suspension) with interpolation; does it jitter or creep when parked; is it deterministic (two runs bit-identical); does anything about Jolt leak. Record the lessons the spec needs.

The spike's scene may later become the C++ vehicle demo scene (see "What do the vehicle demo scenes show?").

From "Which settings does each vehicle kind expose?", also check: wheel settings patched in place on a live vehicle (Jolt has no setters and reads them by reference) take effect; a motorcycle with default `MotorcycleLean` stands at rest and leans in turns; the default tyres (`VehicleTyre` peak 1.2, slide 1.0) drive well without the car sample's ×10 longitudinal cap (if not, raise the default friction, never add the cap); the all-wheel-drive default with no `VehicleDifferential`; geometric gear spacing from `firstGear` to `topGear`; changing `MotorcycleLean::maxAngle` rebuilds privately and keeps wheel spin, RPM and gear.

From "How does a vehicle step each tick, and what does game code read back?", also check: a handbraked car on a slope stays put and falls asleep (there is no anti-creep helper); ice and grass feel right with tyre and ground friction combined as the geometric mean; wheel poses interpolate smoothly (compression, steer and unwrapped spin blended in chassis space); `VehicleState` and `VehicleWheelState` read sensibly (gear, RPM, compression, slips); a rebuild keeps RPM, gear and surviving wheels' spin.

## Answer

**Verdict: the vehicle model works through the seam, and nothing about Jolt reaches the public interface.** The user drove the DemoApp's "Vehicles (spike)" scene (F6) and judged that it looks right. Two Jolt behaviours needed private fixes: the suspension's bump stop, and the motorcycle's lean damping (lessons 1 and 2). With them, the defaults of "Which settings does each vehicle kind expose?" drive without changes.

The scene has three vehicles:

- a 1500 kg car towing a 300 kg trailer on a `BallJoint`;
- a 240 kg motorcycle, with Jolt's sample's numbers;
- a 4000 kg tank with nine wheels a side.

The track has ten bumps, a 2 m ramp, an ice lane (friction 0.05), a grass lane (0.3), a wall of 20 kg crates and a 15° slope to park on. Tab switches vehicle, and a chase camera follows. Readouts of `VehicleState` and `VehicleWheelState` are in the chassis and wheel `Name`s. Wheels are drawn as striped cubes at their written-back poses.

Prototype (primary source, never merged): branch `prototype/fenrir-physics-vehicles-spike` (`a5c6d8c`). The slice covers:

- the public `Vehicles.hpp`, as tickets 42–44 propose: `Vehicle` with a `VehicleKind` union, `VehicleInput`, `VehicleProbe`, `maxTilt`, `enabled` and `VehicleState`; `VehicleWheel` with `VehicleWheelState`; `VehicleDifferential`; `VehicleAntiRollBar`;
- a backend vehicle per chassis, made, rebuilt (kind, wheel set, `maxAngle`, chassis body remade) or patched in step 2, in chassis entity order, with input pushed and the chassis woken on any edit;
- the vehicle run inside the step as Jolt's step listener, with a private priority from the chassis entity;
- state and wheel poses written back in step 5, and wheel poses blended in `AfterTicks` against the chassis's blended pose.

The headless check is `FenrirPhysicsTests "[vehicle-spike]" -s`, eleven cases that print what they measure. `FENRIR_DEMO_KEYS=1` drives an autopilot lap and presses every key.

**Where it guessed or skipped:**

- trailers and every edge case (ticket 45 was open);
- out-of-range warnings, and inactive warnings for differentials and anti-roll bars (the spike skips them silently);
- a vehicle debug-line category;
- `BeforeTicks` restoring wheel poses;
- C#.

**Evidence** (clang Debug and Release give the same numbers):

- **Seam:** Jolt is still included only in `JoltBackend.cpp`. The new public header and `Vehicles.cpp` name no Jolt type. The backend gained `CreateVehicle`, `DestroyVehicle`, `UpdateVehicle`, `SetVehicleInput`, `ReadVehicle` and `WheelPose`.
- **Builds:** clang-x64-debug and clang-x64-release. All 353 FenrirPhysics test cases pass in Debug. MSVC and Linux were not built.
- **Smoke run:** a DemoApp run with every key pressed raised no warnings and did not crash.
- **Default car, no ×10 cap:** 0–100 km/h in 8.8 s, and 118 km/h after 15 s in fifth. It brakes from there to sleep in 8 s. The defaults drive well, so no friction change is needed.
- **All-wheel-drive default:** a car with no `VehicleDifferential` drives on all four wheels. Jolt's automatic box will not shift up while a wheel slips, so a hard launch holds first gear to the 6000 RPM limit (about 70 km/h).
- **Gear spacing:** held in Manual, each gear's measured engine-to-wheel ratio steps by 1.34–1.38. The expected step is 1.377. Each ratio sits about 0.07 above its nominal value, which is clutch slip.
- **Parking:** handbraked on 10°, the car crept 0 mm and was already asleep. On 20° it crept 13 mm, then slept after 51 ticks. On the flat with no input it does not move and sleeps. No anti-creep helper is needed.
- **Motorcycle:** stands at rest (lean 0.00°, both wheels down). At 60 km/h with full right steer it turns at 0.6 rad/s and leans 23°. `limitSteering` caps the steer at 3.2–3.5°.
- **Tank:** with tracks at −1 and 1, it pivots 630° in 5 s and drifts 1 cm. At rest 14 of its 18 wheels touch (the end wheels sit higher, as in Jolt's sample).
- **Live wheel edits:** springs from 1.5 Hz to 0.8 Hz lower the chassis from 0.704 to 0.650. A radius of 0.45 raises it to 0.800. A front `maxSteerAngle` raised from 0 to 0.5 starts steering at once. Jolt's in-place patching works.
- **Rebuild:** changing `maxAngle` mid-drive gave 8585 → 8596 RPM, gear 1 → 1, and rear spin 115.3 → 115.5 rad/s.
- **Grip** (geometric mean, tyre peak 1.2), after 5 s of throttle and then braking:

  | Ground friction | Speed after 5 s | Braking distance |
  | --- | --- | --- |
  | 0.05 (ice) | 34 km/h, wheels spinning (slip 1.1) | 21.7 m, about 0.22 g |
  | 0.3 (grass) | 72 km/h | 36 m |
  | 0.5 (tarmac) | 79 km/h | 38 m |
  | 1.0 | 80 km/h | 32 m |

  Ice feels like ice and grass is clearly looser. The mean behaves as ticket 44 predicted.
- **Interpolation:** at frames of 0.3 ticks, the wheel moves every frame and stays on its axle relative to the shown chassis.
- **Determinism:** a car, a motorcycle and a tank driving over bumps and crates for 600 ticks gave 16 200 bit-identical poses in two threaded runs. They also matched a single-threaded run.

**Lessons for the vehicles spec:**

1. **Jolt's hard point must sit above the top of the travel.** Jolt's bump stop engages only below its min suspension length. A probe that starts inside the ground reports length 0.
   - With ticket 43's mapping (min length 0, hard point at the top of the travel), a bottomed-out wheel never stops the chassis. Soft springs sank the car onto its belly.
   - The spike puts the hard point a private margin (the wheel's radius) above the top of the travel, with min length = margin and max = travel + margin.
   - Amend ticket 43: `position` and `travel` keep their meaning, and the margin is private. Compression is clamped to `0..travel` in the read-back.
2. **Jolt's motorcycle lean damping diverges for a light bike.** Jolt applies its lean damping again in every velocity iteration. Once damping × collision-step time exceeds the chassis's roll inertia, each iteration overshoots, and the bike explodes on landing.
   - At Jolt's 1000 and 60 Hz, that limit is 16.7 kg m². The sample bike's box gives 10.4 kg m².
   - Jolt's own sample survives only because its centre-of-mass offset adds inertia.
   - The spike caps the damping at half the roll inertia ÷ the 60 Hz step, privately, and keeps Jolt's spring of 5000. Scaling the spring down as well left it too weak to lean the bike against a turn's roll moment (0.8° instead of 23°).
   - The spec should state the cap as private, as the lean gains already are.
3. **The probe's default cannot depend on the kind.** `Vehicle::probe` is one field, so "tracked defaults to Ray" has no plain-struct form. The tank drives and pivots well on the `Cylinder` default. Make `Cylinder` the one default.
4. **`JointSpring` reuse is a trap with designated initialisers.** `.spring = {.frequency = 2.0f}` gives `JointSpring`'s damping of 1.0, not the suspension's 0.5. The spec should either give `VehicleSuspension` its own spring type, or document that the damping must then be written too.
5. **A chassis must not be scaled to size its look.** Wheel positions follow the chassis's world scale, so a chassis scaled to stretch a cube (the DemoApp's habit for boxes) also stretches where its wheels sit.
   - The scene uses an unscaled chassis with a `BoxShape` and a scaled look as a child.
   - Wheel entities should not be children of the chassis. Physics writes their world pose, and their own scale sizes the wheel mesh.
   - The demo scenes ticket ("What do the vehicle demo scenes show?") and the C# docs should say so.
6. **Manual mode takes forward as a pedal.** Jolt reads forward as [0, 1] in Manual, where the gear sets the direction. The spike clamps a negative forward to 0. The spec should say so on `VehicleInput::forward`.
7. **Grip needs no friction table yet.** Jolt's default combine is already √(tyre × body friction). FenrirPhysics gives each body one material (a compound's is its own entity's or its first collider's). Ticket 44's private sub-shape table only matters once colliders get their own materials, so the spec can use the default until then.
8. **No centre-of-mass control.** Jolt's samples lower the chassis's centre of mass, and FenrirPhysics has no way to. The car (centre of mass 0.8 m up) drove, braked and jumped the ramp without rolling. The bike's only problem was lesson 2. No field is needed now. A centre-of-mass offset is a possible later addition, beyond vehicles.
9. **Trailers** (provisional, for "How does a vehicle meet other bodies, joints and characters?"):
   - An unpowered trailer is a `Vehicle` given no input. It stays in neutral, so its wheels only roll and brake.
   - A `BallJoint` between two vehicles' chassis towed it round the track and over the ramp.
10. **Details for the spec:**
    - Automatic downshifts to neutral while braking to a stop, which is fine.
    - `state.shifting` and the clutch read sensibly through Jolt's switch.
    - The tank's track speeds read −5.84 / 5.84 m/s in the pivot.
    - Wheel order as entity order and the rearmost-wheel driven track worked unchanged.
    - Vehicle input does not wake a chassis in Jolt. Waking on any input or vehicle edit in step 2, as ticket 44 says, is needed and enough.
    - The motorcycle's default AWD drives its front wheel too. The scene's rear-only `VehicleDifferential` (Jolt's ratio 4.825) is what makes it ride like Jolt's sample.
