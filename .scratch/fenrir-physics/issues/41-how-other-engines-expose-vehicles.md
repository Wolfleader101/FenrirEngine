# How do other engines expose vehicles?

Type: research
Status: resolved
Blocked by: none

## Question

How do other engines put vehicles in front of game code? Survey Unity (`WheelCollider` on a `Rigidbody`, and Unity Physics / DOTS vehicle samples), Unreal (Chaos Vehicles: `ChaosWheeledVehicleMovementComponent`, wheel classes, engine and transmission setup), Godot (`VehicleBody3D`, `VehicleWheel3D`), O3DE (PhysX vehicles, if shipped), Bevy (bevy_rapier's and Rapier's `DynamicRayCastVehicleController`, Avian vehicle examples or crates), and PhysX's own vehicle SDK as the model most engines wrap, from their docs and source. For each:

- **Shape of the API:** where the vehicle lives (a component on the body, a separate entity, a controller object); where the wheels live (child nodes or entities, an array on the vehicle, components of their own); how wheel positions are given (local offsets, child transforms).
- **Kinds:** cars, motorcycles (leaning, staying upright), tracked vehicles; whether one model covers them or each has its own.
- **Driver input:** throttle, steering, brake, handbrake, gear; fields set every frame or calls; normalised or physical units; where it is applied relative to the physics step.
- **Drivetrain and tuning:** engine curve, gearbox, differentials, anti-roll bars, suspension, tyre friction; which engines expose all of it, which simplify it, and what defaults make a car drive out of the box.
- **Read-back:** speed, engine RPM, gear, per-wheel contact (grounded, ground body, normal, slip, suspension compression); written back or queried.
- **Surfaces:** per-surface grip (from the ground's physics material, a callback, a lookup table).
- **Wheel visuals:** whether the engine poses wheel meshes itself (spin, steer, suspension), and how.
- **Other bodies and characters:** driving over dynamic bodies, trailers joined by joints, a character entering and sitting in a vehicle, and the vehicle not colliding with its own driver (collision groups, ignore lists, layers).
- **ECS fit** (Bevy and Unity DOTS especially): which components, and how a vehicle update fits a system.
- **Scripting access**, and **common complaints** (jitter at low speed, sliding when parked, flipping, wheels sinking or bouncing, tunnelling at speed, motorcycles falling over, tuning being hard).

Put the findings on a `research/engine-vehicle-models` branch, in `.scratch/fenrir-physics/research/engine-vehicle-models.md`, and link them here.

## Answer

Findings: branch `research/engine-vehicle-models`, `.scratch/fenrir-physics/research/engine-vehicle-models.md` (commit 7e98980): one section per engine (Unity `WheelCollider`, the Unity Physics DOTS sample and the `com.unity.vehicles` ECS package, Unreal Chaos Vehicles, Godot, Rapier and bevy_rapier, Avian, O3DE, PhysX 5 vehicles), a comparison table, and the options for FenrirPhysics with no choice made. Sources are pinned to commits or versions; Unreal is from the 5.8 docs only (its source needs a login), and Unity's ECS package from its tarball (no public repository).

- **API shape:** every engine models a car as one chassis body plus a ray or shape cast per wheel; wheels are never bodies (except O3DE's ROS 2 gem). Godot's and Rapier's controllers are ports of Bullet's `btRaycastVehicle`. Avian has none and calls one out of scope; O3DE's engine ships none.
- **Where wheels live:** an array on the vehicle (Unreal's `WheelSetups`, bevy_rapier, PhysX, O3DE's proposal), a node or entity per wheel (Unity's `WheelCollider`, Godot's `VehicleWheel3D`, Unity's ECS package with wheel entities and a buffer on the vehicle), or a jointed body per wheel (O3DE's ROS 2 gem only).
- **Kinds:** one model covers any number of wheels. Only PhysX has a tank drive; no engine keeps a motorcycle upright (Unreal offers arcade stabilising only); Unreal has no tracks, and many-wheeled tanks slide.
- **Driver input:** physical per-wheel values (Unity `WheelCollider`, Godot, Rapier) or normalised pedals and steering, throttle and brake in [0, 1], steer in [−1, 1], plus gear and handbrake (Unreal, PhysX, Unity ECS). Always written before the step. Unreal shapes input (rise and fall rates, reverse-as-brake).
- **Drivetrain and defaults:** none in Godot, Rapier and `WheelCollider` (game code sets per-wheel force); an engine curve, gearbox, differential and aerodynamics in Unreal and Unity ECS; the full, replaceable set in PhysX. Unity ECS has the clearest out-of-the-box defaults (engine starts on awake, automatic six-speed, 12 sub-steps). Godot's docs advise against its own Bullet defaults.
- **Read-back:** queried through calls in Unity, Unreal and Godot; written back into data each tick by bevy_rapier (`VehicleWheel.state`, with the ground entity) and Unity ECS (contact, compression, slip, impulses, gear, RPM); caller-owned structs in PhysX.
- **Surfaces:** only PhysX maps the ground's material to friction (a lookup table with a default); Unreal takes a surface coefficient and reports the material; Unity, Godot and Rapier ignore it (Godot's docs claim otherwise).
- **Wheel visuals:** the engine poses wheels itself in Godot and Unity ECS; offers a pose to copy in Unity (`GetWorldPose`), bevy_rapier and PhysX; Unreal uses an animation node on wheel bones.
- **Other bodies and characters:** every engine keeps the chassis out of its own wheel casts; a driver is kept off the wheels by exclude sets or groups (bevy_rapier), trace channels (Unreal), query filter data (PhysX), or only the collision mask (Godot, whose exceptions don't reach the rays). Godot and Rapier never push back the body under the wheels; Unity's ECS work does. Trailers are ordinary joints everywhere. No engine has a concept of entering a vehicle (Unreal's possession is the nearest).
- **ECS fit:** bevy_rapier puts settings, input and output in one component with a `Vec` of wheels on the chassis; Unity's ECS package splits settings (wheel entities) from state (a buffer on the vehicle) and raises wheel added and removed events; Unity's sample runs the vehicle between building and simulating the physics world.
- **Scripting:** C# in Unity, Blueprint and C++ in Unreal, GDScript and C# in Godot; PhysX and O3DE's gem are C++ only.
- **Common complaints:** sliding or jitter when parked or slow is the most common (remedies: Unity ECS's anti-creep anchor, PhysX's sticky-tyre constraints, Unreal's aggressive sleep); wheels popping over kerbs with one ray, sinking or bouncing; Godot breaking below 60 ticks per second and losing the vehicle on freeze; hard tuning in Unity and Unreal; Rapier's infinite braking friction at zero wheel spin.
