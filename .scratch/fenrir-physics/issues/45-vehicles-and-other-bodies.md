# How does a vehicle meet other bodies, joints and characters?

Type: grilling
Status: open
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
