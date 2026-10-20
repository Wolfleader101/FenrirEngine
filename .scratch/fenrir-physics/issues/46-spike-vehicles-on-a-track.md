# Spike: a car, a motorcycle and a tank driving a small track through the proposed model

Type: prototype
Status: open
Blocked by: 42, 43, 44

## Question

Do the vehicle model from [How is a vehicle modelled in the ECS?](42-vehicles-in-the-ecs.md), the settings from [Which settings does each vehicle kind expose?](43-vehicle-kinds-and-settings.md) and the per-tick step from [How does a vehicle step each tick, and what does game code read back?](44-how-a-vehicle-steps-and-reports.md) work in practice? Build a throwaway spike in the DemoApp on a `prototype/fenrir-physics-vehicles-spike` branch, in its own worktree: a car driven from the keyboard round a small track with a ramp, a bumpy section, a low-grip patch (a `PhysicsMaterial`), loose boxes to drive through, and a trailer on a `Joint`, plus a motorcycle and a tank to switch to; wheels drawn from their written-back poses; a follow camera. Made through the proposed public interface with Jolt behind the seam. Check it by hand in the DemoApp, as the earlier spikes were.

React to it: does the interface read well; does a car with default settings drive well; do motorcycles stay up and lean; do tanks turn on the spot; do wheels look right (spin, steer, suspension) with interpolation; does it jitter or creep when parked; is it deterministic (two runs bit-identical); does anything about Jolt leak. Record the lessons the spec needs.

The spike's scene may later become the C++ vehicle demo scene (see "What do the vehicle demo scenes show?").

From "Which settings does each vehicle kind expose?", also check: wheel settings patched in place on a live vehicle (Jolt has no setters and reads them by reference) take effect; a motorcycle with default `MotorcycleLean` stands at rest and leans in turns; the default tyres (`VehicleTyre` peak 1.2, slide 1.0) drive well without the car sample's ×10 longitudinal cap (if not, raise the default friction, never add the cap); the all-wheel-drive default with no `VehicleDifferential`; geometric gear spacing from `firstGear` to `topGear`; changing `MotorcycleLean::maxAngle` rebuilds privately and keeps wheel spin, RPM and gear.

From "How does a vehicle step each tick, and what does game code read back?", also check: a handbraked car on a slope stays put and falls asleep (there is no anti-creep helper); ice and grass feel right with tyre and ground friction combined as the geometric mean; wheel poses interpolate smoothly (compression, steer and unwrapped spin blended in chassis space); `VehicleState` and `VehicleWheelState` read sensibly (gear, RPM, compression, slips); a rebuild keeps RPM, gear and surviving wheels' spin.
