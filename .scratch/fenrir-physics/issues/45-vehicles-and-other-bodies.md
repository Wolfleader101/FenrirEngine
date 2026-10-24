# How does a vehicle meet other bodies, joints and characters?

Type: grilling
Status: resolved
Blocked by: 42, 44

## Question

How do vehicles interact with the rest of the physics world, and where are their edges? Builds on [How is a vehicle modelled in the ECS?](42-vehicles-in-the-ecs.md) and [How does a vehicle step each tick, and what does game code read back?](44-how-a-vehicle-steps-and-reports.md), and follows "How does a character interact with other bodies?" and "What happens at a joint's edges?".

- **Ground filtering:** which bodies the wheels' ground casts see (the chassis's `CollisionFilter` layer and mask, triggers never, the vehicle's own body never).
- **Other bodies:** driving over dynamic bodies and pushing them; driving onto kinematic platforms; being hit; queries seeing the vehicle as its chassis body.
- **Trailers:** a trailer is a second body (or vehicle) joined by an existing `Joint`; what has to hold for that to work (both ends dynamic, `collideConnected`, wheels on an unpowered trailer).
- **Characters and vehicles:** whether a character getting in, riding and getting out is part of this spec, and if so how a character stops colliding with the vehicle it sits in. The map's fog says general collision groups come with vehicles or ragdolls; this ticket decides whether vehicles need them, or whether disabling the character, layers or a joint are enough.
- **Edges:** a vehicle whose chassis loses its `RigidBody` or becomes non-dynamic; a chassis shape or scale change; a wheel entity destroyed mid-drive; flipped and stuck vehicles (game code's job, or a helper).
- **Robustness:** one warning per invalid setup, inactive until valid, as for joints and characters.

Facts from "What does Jolt offer for vehicles?": each probe takes one object layer (by default the two-sided simulation filters) with overridable broad-phase, object-layer and body filters held as raw pointers; FenrirPhysics's layer registry fits either the body's own layer or its query filters. Probes skip triggers. Wheels push and wake dynamic bodies under them and ride moving ground through tyre friction; only the hull collides. The constraint disables itself while its body is out of the broad phase and ignores shape changes. Resetting a gravity override sets the body's gravity factor to 1.

From "How do other engines expose vehicles?": every engine keeps the chassis out of its own wheel casts; a driver is kept off the wheels by exclude sets or groups (bevy_rapier), trace channels (Unreal), query filter data (PhysX), or only the collision mask (Godot). Godot and Rapier never push back the body under the wheels. Trailers are ordinary joints everywhere. No engine has a concept of entering a vehicle. Godot loses the vehicle when its body is frozen and unfrozen.

From "Which settings does each vehicle kind expose?": `Vehicle::maxTilt` (the pitch and roll cone) defaults to π, off, so flipped vehicles are this ticket's to handle; `VehicleDifferential` and `VehicleAntiRollBar` are components on any entity naming two wheel entities, inactive (one warning) unless both are active wheels of one vehicle, so a wheel destroyed mid-drive makes them inactive; a wheeled vehicle with no differential drives every wheel.

## Answer

No ADR: the trailer kind is one more `VehicleKind` alternative, addable or removable like any other, and everything else follows ADR-0015, the joints edge rules ("What happens at a joint's edges?", "How do jointed bodies stop colliding?"), the character rules ("How does a character interact with other bodies?") and the vehicle step rules ("How does a vehicle step each tick, and what does game code read back?"). Glossary: **trailer** added (and to **vehicle kind**); **vehicle** covers towed vehicles; **wheel** never stands on a character body. General collision groups: vehicles need none.

- **Ground filtering:** each wheel's probe filters exactly as the chassis does, under the two-sided layer-and-mask rule of its `CollisionFilter` (privately, the chassis body's own object layer with Jolt's default filters). There is no separate probe mask. Probes always skip triggers, the vehicle's own chassis and every character body. They ignore joints, as all queries do, so `collideConnected` never hides ground from a wheel.
- **Characters are never ground:** wheels skip character bodies (privately, the probe's body filter drops inner bodies and the chassis). A car meeting a pedestrian meets them with its hull, never by climbing a wheel up the capsule. To roll over something, make it a dynamic or static body.
- **Other bodies, as Jolt does them, with no new fields:**
  - A wheel on a dynamic body pushes and loads it and wakes it (unlike Godot and Rapier).
  - On kinematic or other moving ground, slip is measured against the ground's velocity at the contact, so a vehicle rides a platform through tyre grip. Nothing like a character's carried velocity is needed.
  - The chassis is an ordinary dynamic body: it is hit, pushed, jointed and reported by contact events like any other.
  - Queries see only the chassis and name its entity. Wheel entities are not bodies, so `QueryFilter::ignoreBody` takes the chassis.
  - A body falling onto a wheel's drawn mesh passes through it and meets the hull.
  - **Stated limit:** over light bodies the chassis dips, an artifact of the solver's mass ratio. There is no `probeMask` to filter rubble out of the probes alone; it can be added later beside the chassis's filter.
- **Characters and vehicles:** no new behaviour; the spec states what follows from the character rules.
  - A character pushing a vehicle pushes with its `pushForce` (100 N by default), which barely moves a 1500 kg car.
  - A vehicle hitting a character meets an immovable kinematic wall and stops, because dynamic bodies never push characters. Running people over is game code's job: layers that let the hull pass, plus a contact event or `MoveCast` that writes knockback `velocity`.
  - A character standing on a chassis (a truck bed, a roof) rides it as it rides any ground, and presses down on it with its weight.
- **Getting in and out is game code, not engine code:** there is no enter or exit concept and no seat component (no engine surveyed has one). The spec gives a recipe:
  - Getting in: set the character's `enabled = false`, which hides it from bodies, queries and triggers. Then parent its entity under the chassis or a seat child at a seat-local `Transform`. A disabled character is never posed by physics and is never part of the chassis compound (ADR-0014), so it follows the chassis's interpolated pose.
  - Getting out: unparent it, place it beside the door, clear it with `Depenetrate` or `MoveCast`, then re-enable it.

  The C++ vehicle demo scene does this as demo code.
- **Trailers:** a new kind, `TrailerVehicle {}`, with no fields.
  - It has no engine and no gearbox. It reads `brake`, `handbrake` and `steer` (for a steered dolly) and ignores `forward`, gear and clutch. Privately it is Jolt's wheeled controller with no differentials.
  - It uses `Vehicle`'s shared settings: the probe defaults to `Cylinder`, as for wheeled vehicles, `maxTilt` is off, and `VehicleAntiRollBar` works. `VehicleDifferential`s naming its wheels are ignored, as tracked vehicles ignore them.
  - Its `VehicleState` reports `forwardSpeed` and `wheelsOnGround`; the engine and gear fields stay 0.
  - Hand carts and wagons use the same kind. `VehicleKind` now has four alternatives, which "What does C# get for vehicles?" weighs for the `[TaggedUnion]` generator.
- **Towing needs no vehicle-specific joint rules:** a trailer is any dynamic body (usually a trailer vehicle) joined to the chassis by an ordinary `Joint`, usually `ball` at the hitch.
  - With the default `collideConnected = false`, the hitch doesn't fight the hull.
  - The joint links the two bodies' islands, so they wake together.
  - Trailer brakes are game code copying the tractor's `brake` into the trailer's `input`. There is no automatic link.
  - **Stated limit:** a trailer much heavier than its tractor jitters at the hitch, because of the solver's mass ratio.
- **General collision groups: not needed for vehicles.** The driver is a disabled character; a trailer, a turret or a crane arm hinged to the hull is excluded by its joint's `collideConnected`; wheels collide with nothing; and teams of vehicles passing through each other is a matter of layers. Jolt's tank sample uses a group filter only for its turret and barrel, which joints cover here. The fog entry now waits on ragdolls alone.
- **Chassis edges:**
  - **Losing the body:** if the chassis loses its `RigidBody` or its usable collider, or turns kinematic or static, the vehicle goes inactive (ADR-0015): one warning, `state` reset, and wheel entities left where they were. Once it is valid again it is built fresh with nothing carried over, as re-enabling is in 44. A car made kinematic for a cutscene therefore restarts at idle in neutral.
  - **The body rebuilt in one drain** (a collider swapped, or the `RigidBody` removed and added): the vehicle is removed before the body and recreated after it in step 1, as joints are, carrying over RPM, gear and wheel spin under 44's rebuild rule.
  - **A shape or centre-of-mass change:** needs nothing. Wheel positions are from the body origin, and the backend reads mass and inertia each step.
  - **A world-scale change:** wheel positions are scaled (ADR-0015), so physics patches them in place as a live wheel edit, with no rebuild, and wakes the chassis.
  - A teleport is 44's.
- **Wheels destroyed mid-drive:** destroying or unlinking a wheel entity rebuilds the vehicle (ADR-0015). A `VehicleDifferential` or `VehicleAntiRollBar` treats a wheel name that has *died* (the entity destroyed, or its `VehicleWheel` removed) as empty, with one warning; it is not made inactive. It stays inactive (ticket 43) only for names that are wrong rather than dead: a non-wheel entity, or wheels of two different vehicles.
  - A differential left with one wheel drives that wheel, so a rear-wheel-drive car that loses a rear wheel stays rear-wheel drive.
  - An anti-roll bar left with one wheel is inert.
  - **Stated limit:** a differential left with no wheels names no vehicle, so if it was a vehicle's only differential, that vehicle falls back to the all-wheel-drive default.
- **Flipped and stuck vehicles are game code's:** there is no helper, and `maxTilt` stays off by default. The spec gives a recipe. Game code notices `state.wheelsOnGround == 0` with the chassis's up turned away from world up for a while, then either teleports the chassis upright (a `Transform` write) or applies a torque. A `maxTilt` of about 60° (as in Jolt's samples) stops rollovers altogether. The demo scenes have a reset key.
- **Robustness:** nothing new. The inactive rules of ADR-0015 and 43 apply, one warning per invalid setup, back once valid. A `Joint` naming a wheel entity names a non-body, so it is inactive under "What happens at a joint's edges?".
- **Handed on:**
  - "Spike: a car, a motorcycle and a tank driving a small track through the proposed model": drive over a few light boxes (the dip), tow a trailer vehicle on a ball hitch, check a car meeting a character (the hull stops, wheels never climb), and check probes skip the character's inner body.
  - "What does C# get for vehicles?": `TrailerVehicle` as `VehicleKind`'s fourth alternative.
  - "What do the vehicle demo scenes show?": a trailer, the get-in and get-out recipe as demo code in the C++ scene, and a reset key for flipped vehicles.
