# How other engines expose vehicles

Research for [41-how-other-engines-expose-vehicles](../issues/41-how-other-engines-expose-vehicles.md). This file reports how Unity, Unreal, Godot, O3DE, Rapier and bevy_rapier, Avian and the PhysX Vehicle SDK put a wheeled vehicle in front of game code, and what users complain about. It does not choose anything for FenrirPhysics. What Jolt itself offers is ticket 40's subject and is not repeated here.

Sources are engine source, pinned to the commit or package version read, plus official docs and the engines' own issue trackers and forums. Line numbers refer to those versions:

- Unity C# reference source, `Unity 7000.0.0a10` @ [`6c1fc79`](https://github.com/Unity-Technologies/UnityCsReference/tree/6c1fc7919aa64a2698e06922c4516eb1daa91b75), for `WheelCollider`. These are the managed bindings only; the PhysX vehicle code behind them is closed. Unity docs are the 6000.2 Scripting Reference and Manual.
- Unity Physics samples, `EntityComponentSystemSamples` `master` @ [`6786a74`](https://github.com/Unity-Technologies/EntityComponentSystemSamples/tree/6786a741ee1f118ed14cecfa02beae8e926937b0), the "12. Raycast Car" sample.
- Unity's ECS Vehicles package `com.unity.vehicles` 0.1.1-exp.1, read from the package tarball on Unity's registry ([tgz](https://download.packages.unity.com/com.unity.vehicles/-/com.unity.vehicles-0.1.1-exp.1.tgz), SHA-1 `5fc686225b9ccf3f426c34108b403134de97f7e8`). The package has no public repository, so its citations name a path and lines inside that tarball, plus its manual.
- Unreal Engine 5.8: the source needs an Epic account, so this file cites only the official API reference and manual on dev.epicgames.com.
- Godot `master` @ [`4d10419`](https://github.com/godotengine/godot/tree/4d10419cacd6d3009976e8f9fd3abd970fd58018).
- O3DE `development` @ [`74006f5`](https://github.com/o3de/o3de/tree/74006f539d581ea2ae8d21d4ef629d7a610a3e01); the ROS2Controllers gem in `o3de-extras` `development` @ [`6e19291`](https://github.com/o3de/o3de-extras/tree/6e1929143df1f419c3b289d0f6a8b0cb558ed984); and the open pull request [o3de#19270](https://github.com/o3de/o3de/pull/19270) at its head commit [`ab8ab75`](https://github.com/rossbridger/o3de/tree/ab8ab75cec5bf416a5a6367f5d4eb0beb2b0d91b).
- Rapier and bevy_rapier (kept in the Rapier repo) `master` @ [`475caa3`](https://github.com/dimforge/rapier/tree/475caa3ff4646fd9a796b67d14116256f4962bc7).
- Avian `main` @ [`b620b67`](https://github.com/avianphysics/avian/tree/b620b67121def1a88a9eb272763aa24e7d594ccc).
- PhysX 5.6.1, tag `107.3-physx-5.6.1` @ [`5ca9f47`](https://github.com/NVIDIA-Omniverse/PhysX/tree/5ca9f472105a90d70d957c243cb0ef36fe251a9f), and the [5.6.1 Vehicles guide](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Vehicles.html).

Every link is a full URL.

Every engine surveyed models a car the same way underneath: **one rigid body for the chassis, plus a scene query per wheel** (a ray, or a swept wheel shape) that finds the ground, a spring force along the suspension, and tyre forces at the contact. Wheels are not rigid bodies. The PhysX guide states the model most of them inherit ([Mechanical Model](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Vehicles.html#mechanical-model)). Godot's and Rapier's controllers are both ports of Bullet's `btRaycastVehicle`. The only exceptions are O3DE's robotics gem and Rapier's joint example, which build wheels from jointed rigid bodies.

The engines then differ along three lines, and those decide most of the answers below:

- **Where wheels live:** an array on the vehicle (Unreal, bevy_rapier, PhysX, the O3DE proposal), a scene node or entity per wheel (Unity's `WheelCollider`, Godot, Unity's ECS package and sample), or a jointed body per wheel (O3DE's robotics gem).
- **How much drivetrain:** none, so game code sets a force or torque per wheel (Unity `WheelCollider`, Godot, Rapier), or an engine, gearbox and differential behind normalised pedals (Unreal, Unity's ECS package, PhysX).
- **How grip meets the ground:** a per-wheel friction setting that ignores the ground (Unity, Godot, Rapier, Unity's ECS package), or a friction value per ground material (PhysX's lookup table; Unreal's surface friction).

---

## Unity

Unity has three things called vehicles: the built-in `WheelCollider` (GameObjects, over PhysX), a raycast-car sample for Unity Physics (DOTS), and an experimental ECS package, `com.unity.vehicles`. The ECS package is summarised at the end of this section.

### Shape of the API

- **One wheel is one component on its own GameObject.** `WheelCollider` derives from `Collider` ([Vehicles.bindings.cs L89–L92](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Vehicles/Vehicles.bindings.cs#L89-L92)). There is no vehicle component: the wheels that share a `Rigidbody` form a vehicle implicitly. Per-vehicle settings are set through any wheel, for example `ConfigureVehicleSubsteps`, which "sets parameters to the vehicle but not to a wheel" ([L154–L163](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Vehicles/Vehicles.bindings.cs#L154-L163)), and `ResetSprungMasses` ([L151–L153](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Vehicles/Vehicles.bindings.cs#L151-L153)).
- **Positions are the GameObject's transform plus `center`**, "measured in the object's local space" at full suspension extension ([L96–L98](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Vehicles/Vehicles.bindings.cs#L96-L98)). The ground is found by "casting a ray from center downwards the local y-axis" ([L73–L81](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Vehicles/Vehicles.bindings.cs#L73-L81)).
- The tutorial's hierarchy is a `Car` root with the `Rigidbody`, a convex `MeshCollider` on the body, wheel meshes, and separate wheel-collider GameObjects that are not children of the wheel meshes ([Create a car with Wheel colliders](https://docs.unity3d.com/6000.2/Documentation/Manual/WheelColliderTutorial.html)).

### Kinds

Cars only. Nothing in the bindings or the Manual covers motorcycles or tracks.

### Driver input

- **Per-wheel physical properties, set every step:** `motorTorque` in N·m, `brakeTorque` in N·m ("Must be positive"), and `steerAngle` in degrees ([L133–L141](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Vehicles/Vehicles.bindings.cs#L133-L141)). "To simulate brakes, do not use negative motor torque."
- There is no throttle, gear, handbrake or steering wheel. The tutorial's `CarControl` script maps input axes to these properties in `FixedUpdate`, cutting torque and steering range as speed rises, and swapping torque for brake when input opposes travel ([tutorial](https://docs.unity3d.com/6000.2/Documentation/Manual/WheelColliderTutorial.html)).

### Drivetrain and tuning

- **No engine, gearbox or differential.** Per wheel: `radius`, `suspensionDistance`, `suspensionSpring` (spring in N/m, damper, `targetPosition` as a fraction of travel), `forceAppPointDistance`, `mass`, `wheelDampingRate`, and forward and sideways `WheelFrictionCurve`s ([L99–L132](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Vehicles/Vehicles.bindings.cs#L99-L132)). No anti-roll bar.
- `sprungMass` is computed automatically from wheel positions relative to the centre of mass, unless set ([L146–L148](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Vehicles/Vehicles.bindings.cs#L146-L148)).
- **Defaults:** mass 20 kg, radius 0.5, damping rate 0.25, suspension distance 0.3, spring 35000, damper 4500, target 0.5, and friction extremum (0.4, 1), asymptote (0.8, 0.5), stiffness 1 ([Wheel collider component reference](https://docs.unity3d.com/6000.2/Documentation/Manual/class-WheelCollider.html)). The tutorial says a 1500 kg `Rigidbody` "suits the default suspension settings", and lowers the centre of mass by 1 m to "improve stability and prevent rolling" ([tutorial](https://docs.unity3d.com/6000.2/Documentation/Manual/WheelColliderTutorial.html)).
- **Sub-steps** split each fixed step and sum the forces, with separate counts above and below a speed threshold ([L154–L163](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Vehicles/Vehicles.bindings.cs#L154-L163)).

### Read-back

- **Queried.** `isGrounded`, `rpm` (read-only) and `rotationSpeed` ([L142–L150](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Vehicles/Vehicles.bindings.cs#L142-L150)).
- `GetGroundHit(out WheelHit)` returns the closest hit: `collider`, `point`, `normal`, `forwardDir`, `sidewaysDir`, `force`, `forwardSlip` and `sidewaysSlip` ([L9–L71, L168–L176](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Vehicles/Vehicles.bindings.cs#L168-L176)). There is no suspension compression; it follows from `GetWorldPose`.
- No vehicle speed; game code reads the `Rigidbody`.

### Surfaces

**Game code swaps the friction curves.** The tyre model "ignore[s] standard PhysicsMaterial settings"; simulating road materials "is done by changing the forwardFriction and sidewaysFriction based on what material the wheel is hitting", found through `GetGroundHit` ([L9–L21, L83–L87](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Vehicles/Vehicles.bindings.cs#L83-L87)).

### Wheel visuals

**Game code poses the meshes.** `GetWorldPose(out pos, out quat)` gives the world pose "accounting for ground contact, suspension limits, steer angle, and rotation angle" ([L164–L167](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Vehicles/Vehicles.bindings.cs#L164-L167)). The tutorial's `WheelControl` copies it onto the wheel mesh in `Update` ([tutorial](https://docs.unity3d.com/6000.2/Documentation/Manual/WheelColliderTutorial.html)). The Manual says the collider "doesn't roll with the ground", so the model must be a separate rotating GameObject ([Introduction to wheel colliders](https://docs.unity3d.com/6000.2/Documentation/Manual/wheel-colliders-introduction.html)).

### Other bodies and characters

- **Driving over dynamic bodies:** the ray reports any collider. The bindings do not say whether the tyre force acts back on the body hit.
- **Trailers:** an ordinary joint between two `Rigidbody`s; nothing vehicle-specific.
- **Entering, and the driver's own collision:** nothing built in. The general tools are layers (the wheel collider has Layer Overrides: priority, include and exclude layers, per the [component reference](https://docs.unity3d.com/6000.2/Documentation/Manual/class-WheelCollider.html)) and `Physics.IgnoreCollision`, which "is not persistent" ([Physics.IgnoreCollision](https://docs.unity3d.com/6000.2/Documentation/ScriptReference/Physics.IgnoreCollision.html)).

### Unity Physics (DOTS) raycast-car sample

A sample, not an API. It shows what a vehicle looks like as plain ECS data:

- **Wheels are entities.** `Wheel { Vehicle, GraphicalRepresentation, UsedForSteering, UsedForDriving, ChassisFromSuspension }` sits on each wheel entity, pointing back at the chassis. The chassis holds `VehicleConfiguration` ("change with low frequency") and `VehicleBody` ("change with high frequency") ([VehicleMechanics.cs L172–L201](https://github.com/Unity-Technologies/EntityComponentSystemSamples/blob/6786a741ee1f118ed14cecfa02beae8e926937b0/PhysicsSamples/Assets/12.%20Raycast%20Car/Scripts/VehicleMechanics.cs#L172-L201)). The baker assumes a chassis → mechanics → suspension → wheel → graphic hierarchy ([L143–L148](https://github.com/Unity-Technologies/EntityComponentSystemSamples/blob/6786a741ee1f118ed14cecfa02beae8e926937b0/PhysicsSamples/Assets/12.%20Raycast%20Car/Scripts/VehicleMechanics.cs#L143-L148)).
- **Input is a desired speed and steering angle.** `VehicleSpeed { TopSpeed, DesiredSpeed, Damping, DriveEngaged }` and `VehicleSteering { MaxSteeringAngle, DesiredSteeringAngle, Damping }` ([VehicleAuthoring.cs L8–L21](https://github.com/Unity-Technologies/EntityComponentSystemSamples/blob/6786a741ee1f118ed14cecfa02beae8e926937b0/PhysicsSamples/Assets/12.%20Raycast%20Car/Scripts/VehicleAuthoring.cs#L8-L21)). An input system in `InitializationSystemGroup` lerps them from a normalised `VehicleInput` singleton ([VehicleInputHandlingSystem.cs L5–L33](https://github.com/Unity-Technologies/EntityComponentSystemSamples/blob/6786a741ee1f118ed14cecfa02beae8e926937b0/PhysicsSamples/Assets/12.%20Raycast%20Car/Scripts/VehicleInputHandlingSystem.cs#L5-L33)).
- **The vehicle system runs between building the physics world and simulating it**, and applies impulses straight to the `PhysicsWorld`, "rather than modifying component data, since they have already been consumed" ([VehicleMechanics.cs L203–L237](https://github.com/Unity-Technologies/EntityComponentSystemSamples/blob/6786a741ee1f118ed14cecfa02beae8e926937b0/PhysicsSamples/Assets/12.%20Raycast%20Car/Scripts/VehicleMechanics.cs#L203-L237)). Each wheel casts a ray with the chassis's collision filter ([L266–L311](https://github.com/Unity-Technologies/EntityComponentSystemSamples/blob/6786a741ee1f118ed14cecfa02beae8e926937b0/PhysicsSamples/Assets/12.%20Raycast%20Car/Scripts/VehicleMechanics.cs#L266-L311)).
- **Friction and drive impulses are applied equally and oppositely to the body hit** ([L415–L456](https://github.com/Unity-Technologies/EntityComponentSystemSamples/blob/6786a741ee1f118ed14cecfa02beae8e926937b0/PhysicsSamples/Assets/12.%20Raycast%20Car/Scripts/VehicleMechanics.cs#L415-L456)); the suspension impulse goes to the chassis only ([L388–L413](https://github.com/Unity-Technologies/EntityComponentSystemSamples/blob/6786a741ee1f118ed14cecfa02beae8e926937b0/PhysicsSamples/Assets/12.%20Raycast%20Car/Scripts/VehicleMechanics.cs#L388-L413)).
- **Visuals:** the same system writes each wheel entity's `LocalTransform` (steer and suspension) and spins its graphic child, through a command buffer ([L334–L386, L461–L467](https://github.com/Unity-Technologies/EntityComponentSystemSamples/blob/6786a741ee1f118ed14cecfa02beae8e926937b0/PhysicsSamples/Assets/12.%20Raycast%20Car/Scripts/VehicleMechanics.cs#L461-L467)).

### Unity's ECS Vehicles package (`com.unity.vehicles`, experimental)

The registry describes it as "a universal vehicle controller for ECS", depending on `com.unity.physics` and `com.unity.entities` 1.3.10 ([registry entry](https://packages.unity.com/com.unity.vehicles)). It is the closest existing design to a plain-data ECS vehicle:

- **Wheels are entities; the vehicle keeps a buffer about them.** A wheel entity holds `Wheel` (settings: `SpinEntity`, radius, width, mass, wheel collider, suspension length, spring rate in N/mm, damping, `AntiRollBarStiffness`, a three-point friction curve, `FrictionCircleStrength`, protector colliders) and `WheelOnVehicleReference { VehicleEntity, Index }` (`package/Runtime/Wheel.cs` L11–L135). The vehicle holds a `DynamicBuffer<WheelOnVehicle>` of per-wheel state (`package/Runtime/WheelOnVehicle.cs` L13–L179). Adding and removing wheels raises `VehicleWheelEvent`s in a buffer (`package/Runtime/Vehicle.cs` L80–L114).
- **Input is a component of normalised fields.** `VehicleControl { RawSteeringInput, RawThrottleInput, RawBrakeInput, HandbrakeInput, ShiftUpInput, ShiftDownInput, EngineStartStopInput }` (`package/Runtime/VehicleControl.cs` L20–L56). The sample writes it every frame from the Input System, before `VehicleControlSystem` (`Samples~/.../MinimalVehicleControlSystem.cs` L20–L53). Engine start, stop and gear shifts can also be sent as `VehicleControlEvent`s (`VehicleControl.cs` L326–L367).
- **The drivetrain and its state share one component.** `VehicleControlData` holds the engine (max and idle angular velocity, max torque, a sampled torque curve, engine braking, inertia, `EngineIsRunning`), the transmission (automatic or not, forward and reverse ratios, final ratio, shift speeds, `TransmissionCurrentGear`), steering smoothing and speed sensitivity, aerodynamic drag and downforce, and the computed `EngineAngularVelocity`, `EngineTorque`, `RelativeForwardSpeed` and `WheelSpeed` (`VehicleControl.cs` L64–L307). Per wheel, `WheelControl` sets steer angle, Ackermann coefficient, and motor, brake and handbrake shares (`package/Runtime/WheelControl.cs` L8–L46).
- **Defaults that drive out of the box:** 12 sub-steps (`Authoring/VehicleAuthoring.cs` L70); a 7000 rpm, 300 N·m engine that starts on awake, an automatic six-speed box (8, 5.5, 4, 3, 2.2, 1.7; reverse −7; final 3) (`Authoring/VehicleControlAuthoring.cs` L44–L153); wheels of 0.35 m, 30 kg, 0.3 m travel, 50 N/mm spring, anti-roll 10000 (`Authoring/WheelAuthoring.cs` L96–L162). The troubleshooting page lists "Vehicle doesn't move" when no torque curve is assigned or the engine is not started ([Troubleshooting](https://docs.unity3d.com/Packages/com.unity.vehicles@0.1/manual/troubleshooting.html)).
- **Read-back is written into the buffer:** `IsGrounded`, the collider-cast `WheelHit`, `SuspensionLength` and a smoothed `VisualSuspensionLength`, `SuspensionImpulse`, `FrictionImpulse`, normalised `FrictionSlip`, `FrictionSpeed`, `AngularVelocity`, `RotationAngle` and `SteerAngle` (`WheelOnVehicle.cs` L54–L141).
- **Parked creep has explicit state:** an anti-creep reference position per wheel, "The wheel will attempt to hold this position when stationary" (`WheelOnVehicle.cs` L152–L164; `Vehicle.cs` L64–L74).
- **Systems:** control runs first in `SimulationSystemGroup`, before the fixed-step group (`VehicleControlSystem.cs` L16–L19); the vehicle physics runs in `AfterPhysicsSystemGroup` (`VehiclePhysicsSystem.cs` L61–L62); a contact-modification job smooths the chassis's contacts with mesh terrain (`VehicleContactModificationSystem.cs` L11–L18). Impulses on other bodies are collected as `DeferredImpulse`s and applied afterwards on one thread (`VehiclePhysicsSystem.cs` L19–L38).
- **Ground detection is a cylinder collider cast** that skips the vehicle's own entity and non-colliding hits (`WheelHitCollector.cs` L57–L74). Friction is the wheel's curve times suspension load; the ground's material is not read (`VehiclePhysicsSystem.cs` L407–L420).
- **Visuals are systems:** a spin system before transforms and a wheel transform system after graphical smoothing pose the wheel and its spin entity (`WheelTransformSystems.cs` L16–L18, L116–L119).
- **Kinds:** cars and trucks. A single wheel (a motorcycle's) is simply left out of the axle pairs ([Vehicle configuration](https://docs.unity3d.com/Packages/com.unity.vehicles@0.1/manual/vehicle-configuration.html)). The Advanced sample's prefabs are a car, monster truck, pickup, truck, and truck with trailer, plus a moving-platform test (`Samples~/Advanced Vehicle Sample/Prefabs/Vehicles`); the truck and trailer are joined by a `ConfigurableJoint` (`TruckAndTrailer.prefab`). The 0.1.1 changelog lists "Fixed truck collision with trailer" for the samples ([registry entry](https://packages.unity.com/com.unity.vehicles)).

### Scripting access

All C#. `WheelCollider` is a `Collider` subclass with properties and methods. The ECS package is plain `IComponentData` and buffers that any system edits.

### Complaints

- **Steps and kerbs:** with one ray, wheels "clip through steps and curbs, then 'pop' up", and the Manual asks for ground "as smooth as possible" ([Introduction to wheel colliders](https://docs.unity3d.com/6000.2/Documentation/Manual/wheel-colliders-introduction.html)).
- **Jitter and drift at rest** is documented in the bindings: a zero `forceAppPointDistance` "contributes to simulation instability"; check it if the vehicle fails to sleep, jitters, or drifts "when no user input is applied" ([L120–L124](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Vehicles/Vehicles.bindings.cs#L120-L124)). The ECS package's troubleshooting page leads with "Vehicle slides or jitters when stationary" and blames settings too stiff for the update rate ([Troubleshooting](https://docs.unity3d.com/Packages/com.unity.vehicles@0.1/manual/troubleshooting.html)).
- **Tuning is hard:** a long forum thread reports that realistic mass "proved unstable", that high stiffness makes the car "jitter from side to side", and that a flipped car touching a wheel sideways "shoots the vehicle upwards" ([The dark art of Unity wheel colliders](https://discussions.unity.com/t/the-dark-art-of-unity-wheel-colliders-what-unity-doesnt-tell-you/840741)).
- Older tracker reports: a wheel collider flying up after a fall, closed Won't Fix ([601955](https://issuetracker-mig.prd.it.unity3d.com/issues/wheel-collider-jumps-to-sky-when-colliding)); wheels spinning when a sibling collider rotates, fixed in 5.3 ([680167](https://issuetracker.unity3d.com/issues/wheel-collider-rotates-erratically-if-sibling-collider-is-rotated)).

---

## Unreal Engine (Chaos Vehicles)

The `ChaosVehiclesPlugin` lives under `Engine/Plugins/Experimental` ([UChaosWheeledVehicleMovementComponent](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/UChaosWheeledVehicleMovementComp-?lang=en-US)). Epic calls it "Unreal Engine's lightweight system for performing vehicle physics simulations" ([Chaos Vehicles](https://dev.epicgames.com/documentation/unreal-engine/chaos-vehicles?lang=en-US)). A newer "Modular Vehicle Integration" plugin is also Experimental ([ChaosModularVehicle](https://dev.epicgames.com/documentation/unreal-engine/API/PluginIndex/ChaosModularVehicle?lang=en-US)) and is not covered here.

### Shape of the API

- **A movement component on a pawn, with a wheel array.** `UChaosWheeledVehicleMovementComponent` derives from `UChaosVehicleMovementComponent` → `UPawnMovementComponent` ([class page](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/UChaosWheeledVehicleMovementComp-?lang=en-US)). The vehicle Blueprint's parent class is `WheeledVehiclePawn`, with "Simulate Physics" on its skeletal mesh ([How to Set up Vehicles](https://dev.epicgames.com/documentation/unreal-engine/how-to-set-up-vehicles-in-unreal-engine?lang=en-US)).
- **Wheels are an array of setups, each naming a wheel class and a bone.** `WheelSetups: TArray<FChaosWheelSetup>`, "Wheels to create"; the instanced `Wheels: TArray<UChaosVehicleWheel*>` are read-only ([class page](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/UChaosWheeledVehicleMovementComp-?lang=en-US)). `FChaosWheelSetup` is `WheelClass`, `BoneName` ("Bone name on mesh to create wheel at") and `AdditionalOffset` ([FChaosWheelSetup](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/FChaosWheelSetup?lang=en-US)). A wheel class is a Blueprint subclass of `UChaosVehicleWheel`, usually one per axle ([How to Set up Vehicles](https://dev.epicgames.com/documentation/unreal-engine/how-to-set-up-vehicles-in-unreal-engine?lang=en-US)).
- **Positions come from skeleton bones**, plus `Offset` on the wheel ([UChaosVehicleWheel](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/UChaosVehicleWheel?lang=en-US)). The guide: "The order you assign the wheels has no bearing on whether it is a front or a rear wheel, only the Bone Name and Wheel Class have any effect" ([How to Set up Vehicles](https://dev.epicgames.com/documentation/unreal-engine/how-to-set-up-vehicles-in-unreal-engine?lang=en-US)).
- **The simulation runs on the physics thread.** The component fills an async input in `Update` and reads output in `ParallelUpdate` ("Get output data from Physics Thread"); `UChaosWheeledVehicleSimulation` performs suspension traces, suspension and friction forces, steering and the mechanical simulation ([class page](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/UChaosWheeledVehicleMovementComp-?lang=en-US); [UChaosWheeledVehicleSimulation](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/UChaosWheeledVehicleSimulation?lang=en-US)).

### Kinds

- **One model for any wheel count.** "There is no limit on the number of wheels", and the assets "are the same no matter whether you are creating an automobile or a motorcycle" ([How to Set up Vehicles](https://dev.epicgames.com/documentation/unreal-engine/how-to-set-up-vehicles-in-unreal-engine?lang=en-US)). Nothing in the component keeps a motorcycle upright beyond the arcade controls below.
- **Arcade helpers:** `StabilizeControl`, `TorqueControl` and `TargetRotationControl` ("Arcade style direct control of vehicle rotation via torque force"), `EnableSelfRighting`, aerofoils and thrusters ([UChaosVehicleMovementComponent](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/UChaosVehicleMovementComponent?lang=en-US)).
- **No tracks.** A forum user building a tank says Chaos Vehicles "doesn't have the means to support" tracked vehicles; many wheels plus arcade yaw torque made a 62-tonne tank slide "like an ice skater" ([forum, August 2026](https://forums.unrealengine.com/t/adding-options-for-tracked-vehicles-in-chaos-vehicles/2747067)).

### Driver input

- **Normalised setter calls on the component:** `SetThrottleInput` and `SetBrakeInput` "[range 0 to 1]", `SetSteeringInput` "[range -1 to 1]", `SetHandbrakeInput(bool)`, `SetChangeUpInput`, `SetChangeDownInput`, `SetTargetGear(GearNum, bImmediate)` "(-1 reverse, 0 neutral, 1+ forward)", `SetUseAutomaticGears`, `SetParked`, plus pitch, roll and yaw for the arcade controls ([UChaosVehicleMovementComponent](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/UChaosVehicleMovementComponent?lang=en-US)).
- **Inputs are smoothed:** `ThrottleInputRate`, `BrakeInputRate`, `SteeringInputRate` and others set the "Rate at which input … can rise and fall". `bReverseAsBrake` and `bThrottleAsBrake` select arcade reverse behaviour (same page).
- **Who may set them:** `SetRequiresControllerForInputs` decides "whether a controller is required to set control inputs", and `SetOverrideController` lets "the player controller of a different pawn … control this vehicle" (same page).
- **Per-wheel overrides** bypass the drivetrain: `SetDriveTorque`, `SetBrakeTorque`, combined by each wheel's `ExternalTorqueCombineMethod` ([class page](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/UChaosWheeledVehicleMovementComp-?lang=en-US); [UChaosVehicleWheel](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/UChaosVehicleWheel?lang=en-US)).
- The guide wires input events to these calls in the vehicle Blueprint's event graph ([How to Set up Vehicles](https://dev.epicgames.com/documentation/unreal-engine/how-to-set-up-vehicles-in-unreal-engine?lang=en-US)).

### Drivetrain and tuning

- **The full chain, switchable.** `EngineSetup`, `TransmissionSetup` and `DifferentialSetup` apply when `bMechanicalSimEnabled`; `bSuspensionEnabled` and `bWheelFrictionEnabled` switch the other stages, and `EnableMechanicalSim`, `EnableSuspension` and `EnableWheelFriction` toggle them at runtime ([class page](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/UChaosWheeledVehicleMovementComp-?lang=en-US)).
- **Engine:** `MaxTorque` (N·m) "multiplied by TorqueCurve", a normalised `TorqueCurve` over RPM, `MaxRPM`, `EngineIdleRPM`, `EngineBrakeEffect`, `EngineRevUpMOI`, `EngineRevDownRate` ([FVehicleEngineConfig](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/FVehicleEngineConfig?lang=en-US)). The guide makes the torque curve a separate curve asset ([How to Set up Vehicles](https://dev.epicgames.com/documentation/unreal-engine/how-to-set-up-vehicles-in-unreal-engine?lang=en-US)).
- **Transmission:** automatic gears and auto reverse, change-up and change-down RPM, forward and reverse ratios, `FinalRatio`, `GearChangeTime`, `TransmissionEfficiency` ([FVehicleTransmissionConfig](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/FVehicleTransmissionConfig?lang=en-US)). **Differential:** a type and `FrontRearSplit` ([FVehicleDifferentialConfig](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/FVehicleDifferentialConfig?lang=en-US)).
- **Per wheel:** radius, width, mass, `CorneringStiffness`, `FrictionForceMultiplier`, slip and skid thresholds, `LateralSlipGraph`, max steer angle, brake and handbrake torque, ABS and traction control, spring rate and preload, damping ratio, max raise and drop, `RollbarScaling` ("Anti-roll effect"), `SweepShape` ("defaults to ray trace") and `SweepType` ([UChaosVehicleWheel](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/UChaosVehicleWheel?lang=en-US)). Runtime setters exist for most of them (`SetSuspensionParams`, `SetWheelRadius`, `SetMaxEngineTorque`, …) ([class page](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/UChaosWheeledVehicleMovementComp-?lang=en-US)).
- **Chassis:** `Mass`, an optional `CenterOfMassOverride` ("prevents the vehicle handling changing when the asset changes"), `InertiaTensorScale`, drag and downforce coefficients ([UChaosVehicleMovementComponent](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/UChaosVehicleMovementComponent?lang=en-US)).
- **Setup to drive:** the guide's minimum is five wheel properties (axle type, radius, handbrake, engine, steering, plus max steer angle), a torque curve, bone names, and input wiring ([How to Set up Vehicles](https://dev.epicgames.com/documentation/unreal-engine/how-to-set-up-vehicles-in-unreal-engine?lang=en-US)).

### Read-back

- **Queried through calls:** `GetForwardSpeed`, `GetEngineRotationSpeed`, `GetEngineMaxRotationSpeed`, `GetCurrentGear`, `GetTargetGear`, `IsParked`, and the current inputs, which the API notes "can use this to feed control to a connected trailer" ([UChaosVehicleMovementComponent](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/UChaosVehicleMovementComponent?lang=en-US)).
- **Per wheel:** `GetWheelState(i)` returns `FWheelStatus`: `bInContact`, `ContactPoint`, `HitLocation`, `PhysMaterial`, `NormalizedSuspensionLength`, `SpringForce`, `SlipAngle`, `bIsSlipping`, `SlipMagnitude`, `bIsSkidding`, `SkidMagnitude`, `SkidNormal`, `DriveTorque`, `BrakeTorque`, `bABSActivated` ([FWheelStatus](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/FWheelStatus?lang=en-US)). The wheel object adds `GetRotationAngle`, `GetSteerAngle`, `GetSuspensionOffset`, `GetWheelAngularVelocity` and `IsInAir` ([UChaosVehicleWheel](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/UChaosVehicleWheel?lang=en-US)). The ground body is not reported, only its material.
- **Snapshots** (`GetSnapshot`, `SetSnapshot`) capture and restore transform, velocities, gear, engine RPM and per-wheel state ([class page](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/UChaosWheeledVehicleMovementComp-?lang=en-US)).

### Surfaces

The wheel simulation takes a surface friction coefficient: "Wheel load force from body weight and the surface friction together determine the grip available at the wheel" ([FSimpleWheelSim](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/ChaosVehiclesCore/FSimpleWheelSim?lang=en-US)). The component reports the contact's `UPhysicalMaterial` per wheel (`GetPhysMaterial`, `FWheelStatus::PhysMaterial`), and game code can scale grip at runtime with `SetWheelFrictionMultiplier` ([class page](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/UChaosWheeledVehicleMovementComp-?lang=en-US)). The reference pages do not say how the material's own friction maps to the coefficient.

### Wheel visuals

**An animation node poses the wheel bones.** The vehicle's Animation Blueprint derives from `VehicleAnimationInstance`, and its `Wheel Controller` node reads each wheel's spin, steering and suspension and "translates the query results to an animation on the bone that the wheel is associated with". Extra suspension bones follow by Look At nodes ([How to Set up Vehicles](https://dev.epicgames.com/documentation/unreal-engine/how-to-set-up-vehicles-in-unreal-engine?lang=en-US)). `SuspensionSmoothing` trades visual smoothness against "momentary visual inter-penetration" ([UChaosVehicleWheel](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/UChaosVehicleWheel?lang=en-US)).

### Other bodies and characters

- **Wheel traces have their own collision responses:** `WheelTraceCollisionResponses`, `SetWheelTraceResponseToChannel` and `SetWheelTraceAllChannels` choose what the wheels see, separately from the chassis's collision ([class page](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/UChaosWheeledVehicleMovementComp-?lang=en-US)).
- **Wheel collision shapes** are in the physics asset: the guide puts spheres on the wheel bones and turns off collision on suspension bones ([How to Set up Vehicles](https://dev.epicgames.com/documentation/unreal-engine/how-to-set-up-vehicles-in-unreal-engine?lang=en-US)); the conversion guide says "the wheels should have collision disabled" ([How to Convert PhysX Vehicles to Chaos](https://dev.epicgames.com/documentation/unreal-engine/how-to-convert-physx-vehicles-to-chaos-in-unreal-engine?lang=en-US)).
- **Trailers:** a physics constraint between pawns, with the tractor's inputs copied across (the getters above).
- **Entering:** the vehicle is a pawn, so a player enters by possession or by `SetOverrideController`. Hiding or ignoring the driver's capsule is game code.

### Scripting access

Blueprint (`BlueprintCallable` setters and getters, `BlueprintPure` snapshot and status break nodes) and C++ overrides of the simulation stages ([class page](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/UChaosWheeledVehicleMovementComp-?lang=en-US)). `ServerUpdateState` replicates inputs to the server ([UChaosVehicleMovementComponent](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/UChaosVehicleMovementComponent?lang=en-US)).

### Complaints

- **Parked and slope behaviour needs sleep hacks:** the component has `SleepThreshold` ("aggressive sleep logic") and `SleepSlopeLimit` ("will sleep up to 30 degree slopes") ([UChaosVehicleMovementComponent](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/UChaosVehicleMovementComponent?lang=en-US)).
- **Tuning outside the intended envelope:** a user could not make a car drift ("the throttle input dies with a slide"); a reply calls it "hard to get the simulated vehicle systems to operate outside of their primary use" ([forum, 2023](https://forums.unrealengine.com/t/chaos-vehicles/1143659)).
- **Tracks** are unsupported (above). **Wheels not animating** and **vehicle not moving** are the conversion guide's own troubleshooting entries ([How to Convert PhysX Vehicles to Chaos](https://dev.epicgames.com/documentation/unreal-engine/how-to-convert-physx-vehicles-to-chaos-in-unreal-engine?lang=en-US)).

---

## Godot

Godot's vehicle is a port of Bullet's raycast vehicle. Both classes carry the note that they have "known issues" and are not "designed to provide realistic 3D vehicle physics" ([VehicleBody3D.xml L10](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/doc/classes/VehicleBody3D.xml#L10); [VehicleWheel3D.xml L8](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/doc/classes/VehicleWheel3D.xml#L8)).

### Shape of the API

- **A body subclass with wheel child nodes.** `VehicleBody3D` inherits `RigidBody3D`; each wheel is a `VehicleWheel3D` (a `Node3D`) child ([VehicleBody3D.xml L2–L7](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/doc/classes/VehicleBody3D.xml#L2-L7)). A wheel registers itself with its parent on entering the tree and captures its local transform as the connection point, suspension direction (−Y) and axle (X) ([vehicle_body_3d.cpp L118–L143](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/vehicle_body_3d.cpp#L118-L143)).
- **Runs in the body's state callback**, after each physics step: `_body_state_changed` casts rays, applies suspension and friction impulses through `PhysicsDirectBodyState3D` ([cpp L930–L1003](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/vehicle_body_3d.cpp#L930-L1003)).
- The node's origin is the centre of gravity; "the origin point is usually kept low", and "The position or orientation of this node shouldn't be changed directly" ([VehicleBody3D.xml L7–L9](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/doc/classes/VehicleBody3D.xml#L7-L9)).

### Kinds

Cars only, one model.

### Driver input

- **Properties, on the body or each wheel.** `VehicleBody3D.engine_force` (N), `brake` (N) and `steering` (radians in code, degrees in the inspector) ([VehicleBody3D.xml L18–L30](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/doc/classes/VehicleBody3D.xml#L18-L30); units at [cpp L1058–L1060](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/vehicle_body_3d.cpp#L1058-L1060)).
- The body's setters copy the value into wheels marked `use_as_traction` or `use_as_steering`, and the brake into every wheel ([cpp L1007–L1040](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/vehicle_body_3d.cpp#L1007-L1040)). Each `VehicleWheel3D` has its own `engine_force`, `brake` and `steering` for per-wheel control ([VehicleWheel3D.xml L53–L70](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/doc/classes/VehicleWheel3D.xml#L53-L70)).
- Physical rather than normalised. The docs suggest 25–50 for acceleration and 25–30 for braking "For a vehicle with a mass set to 1000" ([VehicleBody3D.xml L18–L22](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/doc/classes/VehicleBody3D.xml#L18-L22)).

### Drivetrain and tuning

- **No drivetrain:** "The simulation does not take the effect of gears into account" ([VehicleBody3D.xml L23](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/doc/classes/VehicleBody3D.xml#L23)).
- **Per wheel, with defaults:** `wheel_radius` 0.5, `wheel_rest_length` 0.15, `suspension_travel` 0.2, `suspension_stiffness` 5.88, `damping_compression` 0.83, `damping_relaxation` 0.88, `suspension_max_force` 6000, `wheel_friction_slip` 10.5, `wheel_roll_influence` 0.1 ([VehicleWheel3D.xml L56–L98](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/doc/classes/VehicleWheel3D.xml#L56-L98)). The docs themselves advise friction slip 1.0 "when starting out" and damping around 0.3 "for a normal car", against the 10.5 and 0.83 defaults.
- **Suspension force scales with chassis mass:** the spring force is multiplied by the body's mass ([cpp L545–L583](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/vehicle_body_3d.cpp#L545-L583)), so stiffness behaves as an acceleration. The body's default `mass` is 40 ([VehicleBody3D.xml L26](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/doc/classes/VehicleBody3D.xml#L26)).
- **Roll:** `wheel_roll_influence` lowers the height at which side impulses act; there is no anti-roll bar ([cpp L860–L868](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/vehicle_body_3d.cpp#L860-L868)).

### Read-back

**Queried per wheel:** `is_in_contact()`, `get_contact_point()`, `get_contact_normal()`, `get_contact_body()`, `get_rpm()` and `get_skidinfo()` (1 is full grip, 0 skidding) ([VehicleWheel3D.xml L14–L50](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/doc/classes/VehicleWheel3D.xml#L14-L50)). The suspension length is internal (`m_raycastInfo`, [vehicle_body_3d.h L119–L129](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/vehicle_body_3d.h#L119-L129)); speed comes from the `RigidBody3D`.

### Surfaces

The docs say `wheel_friction_slip` "is combined with the friction setting of the surface the wheel is in contact with" ([VehicleWheel3D.xml L86–L89](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/doc/classes/VehicleWheel3D.xml#L86-L89)). The code does not: the grip limit is suspension force × step × `m_frictionSlip`, with no read of the ground's material ([cpp L807](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/vehicle_body_3d.cpp#L807)). Per-surface grip is game code that changes `wheel_friction_slip` after `get_contact_body()`.

### Wheel visuals

**The engine poses the wheel node itself.** After each step the wheel's transform (steer, spin, suspension) is written to the `VehicleWheel3D` ([cpp L945](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/vehicle_body_3d.cpp#L945)); with physics interpolation on, it is interpolated every frame instead ([cpp L901–L917](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/vehicle_body_3d.cpp#L901-L917)). A wheel mesh is a child of the wheel node, and the body's own mesh "should generally not include meshes for the wheels" ([VehicleBody3D.xml L7](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/doc/classes/VehicleBody3D.xml#L7)).

### Other bodies and characters

- **Wheel rays use the body's `collision_mask` and exclude only the body itself** ([cpp L481–L482, L1063–L1066](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/vehicle_body_3d.cpp#L1063-L1066)). `add_collision_exception_with` ([PhysicsBody3D.xml L15–L19](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/doc/classes/PhysicsBody3D.xml#L15-L19)) does not reach that exclude list, so a driver inside the mask is "ground" to the wheels; the mask is the only switch.
- **Driving over dynamic bodies:** the side impulse accounts for the ground body's velocity and mass ([cpp L600–L660](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/vehicle_body_3d.cpp#L600-L660)), but no impulse is applied back to it: "apply friction impulse on the ground //todo" ([cpp L870–L872](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/vehicle_body_3d.cpp#L870-L872)).
- **Trailers and entering:** a `Joint3D` and game code respectively.

### Scripting access

All bound through `ClassDB`, so GDScript and C# see the same properties and methods.

### Complaints

- **Sliding when parked:** "Vehiclebody3D sliding down slope when parked" ([#88998](https://github.com/godotengine/godot/issues/88998)), "VehicleWheel3D Sinks Through Gaps Smaller Than Itself & VehicleBody3D Slides on Slopes" ([#98079](https://github.com/godotengine/godot/issues/98079)), with an open fix ([PR #117234](https://github.com/godotengine/godot/pull/117234)).
- **Tick rate:** "physics is broken below 60 physics ticks per second" ([#102816](https://github.com/godotengine/godot/issues/102816)).
- **Freezing:** the vehicle "completely annihilates itself" when its body is frozen and unfrozen with wheels on the ground ([#92891](https://github.com/godotengine/godot/issues/92891)); freezing throws errors ([#76764](https://github.com/godotengine/godot/issues/76764)). It did not wake on engine force, brake or steering, an issue closed in the 4.0 milestone ([#52915](https://github.com/godotengine/godot/issues/52915)).
- **Defaults and units:** open PRs to "Improve VehicleBody3d and VehicleWheels3d defaults" ([#122036](https://github.com/godotengine/godot/pull/122036)) and to make suspension force "follow Hooke's Law" ([#112518](https://github.com/godotengine/godot/pull/112518)); a request to "Document how collision layers work in VehicleBody3D" ([#52983](https://github.com/godotengine/godot/issues/52983)).

---

## O3DE

**The engine ships no vehicle.** A search of the tree at [`74006f5`](https://github.com/o3de/o3de/tree/74006f539d581ea2ae8d21d4ef629d7a610a3e01) finds only an editor icon and two physics-material assets named "Vehicle". A feature request says "O3DE is missing any vehicle" and asks for PhysX vehicles; it is open, labelled help-wanted, and a reply points to the ROS2 gem instead ([#17312](https://github.com/o3de/o3de/issues/17312)). Two partial answers exist.

### The ROS2Controllers gem (o3de-extras): jointed bodies

- **Wheels are real rigid bodies on joints.** Each wheel entity names a hinge joint or articulation link; steering is another jointed entity ([WheelDynamicsData.h L16–L36](https://github.com/o3de/o3de-extras/blob/6e1929143df1f419c3b289d0f6a8b0cb558ed984/Gems/ROS2Controllers/Code/Source/VehicleDynamics/WheelDynamicsData.h#L16-L36)).
- **Configuration is axles of entity ids:** `VehicleConfiguration { m_axles, m_wheelbase = 2.0, m_track = 1.0 }`, each `AxleConfiguration { m_axleWheels, m_axleTag, m_wheelRadius = 0.35, m_isSteering, m_isDrive }` ([VehicleConfiguration.h L17–L28](https://github.com/o3de/o3de-extras/blob/6e1929143df1f419c3b289d0f6a8b0cb558ed984/Gems/ROS2Controllers/Code/Include/ROS2Controllers/VehicleDynamics/VehicleConfiguration.h#L17-L28); [AxleConfiguration.h L17–L47](https://github.com/o3de/o3de-extras/blob/6e1929143df1f419c3b289d0f6a8b0cb558ed984/Gems/ROS2Controllers/Code/Include/ROS2Controllers/VehicleDynamics/AxleConfiguration.h#L17-L47)).
- **Kinds:** Ackermann and skid-steering drive models (`DriveModels/AckermannDriveModel`, `SkidSteeringDriveModel`). Skid steering is how it does tracked or differential-drive robots.
- **Input is target speeds, through an EBus:** `SetTargetLinearSpeed`, `SetTargetSteering`, `SetTargetAngularSpeed`, and fraction variants in [−1, 1]. "Inputs are valid for a short time (configurable) and need to be repeated" ([VehicleInputControlBus.h L17–L77](https://github.com/o3de/o3de-extras/blob/6e1929143df1f419c3b289d0f6a8b0cb558ed984/Gems/ROS2Controllers/Code/Include/ROS2Controllers/VehicleDynamics/VehicleInputControlBus.h#L17-L77)); the default timeout is 200 ms ([VehicleInputs.h L16–L52](https://github.com/o3de/o3de-extras/blob/6e1929143df1f419c3b289d0f6a8b0cb558ed984/Gems/ROS2Controllers/Code/Source/VehicleDynamics/VehicleInputs.h#L16-L52)).
- **Drive is joint motors under PID control,** applied on the frame `OnTick`, not the physics step ([VehicleModelComponent.cpp L127–L131](https://github.com/o3de/o3de-extras/blob/6e1929143df1f419c3b289d0f6a8b0cb558ed984/Gems/ROS2Controllers/Code/Source/VehicleDynamics/VehicleModelComponent.cpp#L127-L131)). Steering sets each steering joint's velocity from a PID on its angle, with Ackermann inner and outer angles from wheelbase and track ([AckermannDriveModel.cpp L77–L130](https://github.com/o3de/o3de-extras/blob/6e1929143df1f419c3b289d0f6a8b0cb558ed984/Gems/ROS2Controllers/Code/Source/VehicleDynamics/DriveModels/AckermannDriveModel.cpp#L77-L130)).
- **Read-back:** only `GetWheelsOdometry()`, linear and angular velocity from the wheels ([VehicleInputControlBus.h L71–L73](https://github.com/o3de/o3de-extras/blob/6e1929143df1f419c3b289d0f6a8b0cb558ed984/Gems/ROS2Controllers/Code/Include/ROS2Controllers/VehicleDynamics/VehicleInputControlBus.h#L71-L73)). Contact, surfaces and visuals are those of ordinary PhysX bodies and their materials.

### A proposed engine-neutral vehicle API (open PR)

[o3de#19270](https://github.com/o3de/o3de/pull/19270), "Add a minimal API for vehicles in AzPhysics", open with two approvals as of September 2026, adds interfaces any backend would implement:

- **A vehicle is a simulated body with a wheel array.** `VehicleConfiguration` extends the rigid-body configuration with up and forward directions, `m_wheels` (one `WheelConfiguration` each), `m_antiRollBars` (wheel index pairs and stiffness) and an optional drive model; `EngineDriveModelConfiguration` adds engine inertia, idle and max speed, peak torque, torque curve, gear ratios and switch time ([Vehicle.h L28–L96](https://github.com/rossbridger/o3de/blob/ab8ab75cec5bf416a5a6367f5d4eb0beb2b0d91b/Code/Framework/AzFramework/AzFramework/Physics/Vehicle.h#L28-L96)).
- **Controls are setters:** `SetThrottle`, `SetBrake`, `SetHandbrake`, `SetSteeringAngle`; wheels by index ([Vehicle.h L98–L124](https://github.com/rossbridger/o3de/blob/ab8ab75cec5bf416a5a6367f5d4eb0beb2b0d91b/Code/Framework/AzFramework/AzFramework/Physics/Vehicle.h#L98-L124)).
- **A wheel is also a simulated body** with radius, width, mass, inertia, damping and suspension settings, and getters for rotation, contact (position, normal, velocity, body handle, shape), friction, slip and `GetJounce()` ([Wheel.h L30–L81](https://github.com/rossbridger/o3de/blob/ab8ab75cec5bf416a5a6367f5d4eb0beb2b0d91b/Code/Framework/AzFramework/AzFramework/Physics/Wheel.h#L30-L81)).
- In review, the author proposed stripping "all engine-specific parameters"; one reviewer compared the Jolt and PhysX wheel settings (PhysX's wheel mass, inertia and damping have no Jolt counterpart); another asked whether wheels should be ECS components ([PR discussion](https://github.com/o3de/o3de/pull/19270)).

### Scripting access

The ROS2 gem's controls are an EBus in C++, also driven from ROS 2 topics. The PR's interfaces are C++.

### Complaints

The main one is absence ([#17312](https://github.com/o3de/o3de/issues/17312)).

---

## Rapier and bevy_rapier

Rapier's `DynamicRayCastVehicleController` is "ported and modified from Bullet's `btRaycastVehicle`" ([ray_cast_vehicle_controller.rs L1](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/src/control/ray_cast_vehicle_controller.rs#L1)), the same ancestor as Godot's. It is 3D only ([vehicle_controller.mdx L13–L20](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/website/docs/user_guides/templates/vehicle_controller.mdx#L13-L20)).

### Shape of the API

- **Rapier: a controller object beside the world.** It holds the chassis's `RigidBodyHandle`, a private `Vec<Wheel>`, up and forward axis indices (+Y up, +X forward by default) and `current_vehicle_speed` ([L17–L31, L257–L268](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/src/control/ray_cast_vehicle_controller.rs#L257-L268)). `add_wheel(chassis_connection_cs, direction_cs, axle_cs, suspension_rest_length, radius, &tuning)` takes positions and directions in the chassis's local space ([L273–L300](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/src/control/ray_cast_vehicle_controller.rs#L273-L300)).
- **Game code calls `update_vehicle(dt, queries)` before each step** ([L409](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/src/control/ray_cast_vehicle_controller.rs#L409-L489)). The guide says "It is strongly recommended to exclude the chassis from the ray-casts, otherwise the ray might hit it" ([mdx L93](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/website/docs/user_guides/templates/vehicle_controller.mdx#L93)).
- **bevy_rapier: a component on the chassis entity, with wheels as a `Vec` field.** `RayCastVehicleController { wheels: Vec<VehicleWheel>, index_up_axis, index_forward_axis, filter_flags, filter_groups, exclude_colliders, exclude_rigid_bodies, filter_predicate, current_vehicle_speed, raw }`, where the chassis "must be a dynamic rigid-body on the same entity" ([control/vehicle_controller.rs L154–L201](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/control/vehicle_controller.rs#L154-L201)). Each `VehicleWheel` holds its geometry, tuning, controls and a written-back `state` ([L50–L81](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/control/vehicle_controller.rs#L50-L81)). "Wheels can be added, removed or modified at any time"; the plugin rebuilds the Rapier controller when the count changes ([systems/vehicle_controller.rs L15–L55](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/systems/vehicle_controller.rs#L15-L55)).

### Kinds

One model, any wheel count. No drivetrain, motorcycle or track support. A separate example builds a vehicle from rigid bodies and joints ([`examples3d/vehicle_joints3.rs`](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/examples3d/vehicle_joints3.rs)); the guide calls that "possible but can be difficult to control" ([mdx L4–L8](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/website/docs/user_guides/templates/vehicle_controller.mdx#L4-L8)).

### Driver input

**Per-wheel physical fields:** `engine_force`, `brake` ("The maximum braking impulse") and `steering` in radians ([bevy vehicle_controller.rs L73–L78](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/control/vehicle_controller.rs#L73-L78)). The bevy example sets them in `Update` from the keyboard, front wheels only ([vehicle_controller3.rs L136–L168](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/bevy_rapier3d/examples/vehicle_controller3.rs#L136-L168)). The plugin's `update_vehicle_controllers` system copies them in "before the simulation step" ([systems/vehicle_controller.rs L83–L165](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/systems/vehicle_controller.rs#L83-L165)), in `PhysicsSet::SyncBackend` ([plugin.rs L184](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/plugin.rs#L184)).

### Drivetrain and tuning

- `WheelTuning` defaults: suspension stiffness 5.88, compression 0.83, damping 0.88, max travel 5.0, side friction stiffness 1.0, friction slip 10.5, max force 6000 ([L35–L70](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/src/control/ray_cast_vehicle_controller.rs#L35-L70)). These are Bullet's numbers, shared with Godot. Each wheel's roll influence defaults to 0.1 ([L200](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/src/control/ray_cast_vehicle_controller.rs#L200)).
- bevy_rapier's own wheel default is a 0.5 m rest length and 0.5 m radius, suspension along −Y and axle along +Z ([bevy vehicle_controller.rs L83–L98](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/control/vehicle_controller.rs#L83-L98)).
- The forces are "computed once per update of the plugin, using the length of the last simulation timestep. So it is best used with a fixed timestep" ([mdx L126](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/website/docs/user_guides/templates/vehicle_controller.mdx#L126)).

### Read-back

- **Rapier:** `wheels()` exposes each wheel's `raycast_info` (contact normal and point, suspension length, hard point, `is_in_contact`, `ground_object` collider), rotation and suspension force ([L238–L251, L491–L499](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/src/control/ray_cast_vehicle_controller.rs#L238-L251)), and `current_vehicle_speed`, signed by the forward axis ([L417–L423](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/src/control/ray_cast_vehicle_controller.rs#L417-L423)).
- **bevy_rapier writes it back into the component:** `VehicleWheelState { rotation, suspension_length, is_in_contact, contact_point, contact_normal, hard_point, center, suspension_direction, axle_direction, ground_entity, suspension_force, forward_impulse, side_impulse }` ([L12–L48](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/control/vehicle_controller.rs#L12-L48)), plus `current_vehicle_speed` and `any_wheel_in_contact()` ([L192–L238](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/control/vehicle_controller.rs#L192-L238)). The ground is reported as an `Entity`. There is no slip value; `forward_impulse` and `side_impulse` stand in for it.

### Surfaces

None. The grip limit is suspension force × dt × `friction_slip` ([L643](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/src/control/ray_cast_vehicle_controller.rs#L643)); the ground collider's friction is not read.

### Wheel visuals

Not posed by the engine: "there is no actual per-wheel rigid-bodies to read their state from" ([mdx L163](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/website/docs/user_guides/templates/vehicle_controller.mdx#L163)). bevy_rapier offers `VehicleWheel::local_transform()`, "The wheel's transform relative to the chassis, for rendering", combining suspension, steering and spin ([L122–L133](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/control/vehicle_controller.rs#L122-L133)). The example copies it onto child entities in `PostUpdate`, after writeback and before transform propagation ([vehicle_controller3.rs L28–L37, L170–L182](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/bevy_rapier3d/examples/vehicle_controller3.rs#L170-L182)).

### Other bodies and characters

- **The chassis is always excluded,** because "The wheels' rays start inside the chassis, so it must never be hit" ([systems/vehicle_controller.rs L143–L151](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/systems/vehicle_controller.rs#L143-L151)). Other colliders are excluded by `filter_flags` (sensors by default), `filter_groups`, the exclude sets, a predicate, or a `ControllerIgnored` component ([vehicle_controller.rs L177–L191](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/control/vehicle_controller.rs#L177-L191)). A seated driver is excluded from the wheels this way; its contacts with the chassis are a collision-group matter.
- **Dynamic ground:** the side impulse is solved against a dynamic ground body ([L577–L595](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/src/control/ray_cast_vehicle_controller.rs#L577-L595)), but "TODO: apply friction impulse on the ground" ([L702–L708](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/src/control/ray_cast_vehicle_controller.rs#L702-L708)): the body under the wheels is not pushed back.
- **Sleep:** the chassis is woken only when a wheel's `engine_force > 0.0` ([L442–L444](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/src/control/ray_cast_vehicle_controller.rs#L442-L444)).

### ECS fit

- **Components:** one `RayCastVehicleController` on the chassis entity, beside `RigidBody::Dynamic` and the chassis `Collider`. Wheel visuals are ordinary child entities that read it.
- **A drive is a field write** to `wheels[i].engine_force` and friends in any system before the plugin's sets. Results arrive in the same component's `state`.
- The component is `Reflect`, except the filter fields and the raw controller ([L164–L200](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/control/vehicle_controller.rs#L164-L200)). Settings, inputs and output share one component, so a system that only reads speed still borrows the inputs.

### Scripting access

Rust, plus Rapier's JavaScript, Python and C bindings ([mdx](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/website/docs/user_guides/templates/vehicle_controller.mdx)).

### Complaints

- **Wheel spin read-back:** rotation only updated while steering ([rapier #925](https://github.com/dimforge/rapier/issues/925); fix in [#945](https://github.com/dimforge/rapier/pull/945)).
- **Braking:** "RayCastVehicle has infinite effective braking friction if it has 0 rotation values" ([rapier #946](https://github.com/dimforge/rapier/issues/946)).
- **A long wait for Bevy support:** "Port the rapier repo's `ray_cast_vehicle_controller` to bevy_rapier" is still open in the old repository ([bevy_rapier #314](https://github.com/dimforge/bevy_rapier/issues/314)), although the in-tree bindings at this commit have the component. A user porting the joint example found "the steering move the body instead of the axle" ([bevy_rapier #582](https://github.com/dimforge/bevy_rapier/issues/582)).

---

## Avian (Bevy)

**No vehicle.** Avian's maintainer calls a vehicle controller "out of scope for built-in support" ([comment on #441](https://github.com/avianphysics/avian/issues/441)). "Add an example for creating a vehicle like a car" has been open, without discussion, since July 2024 ([#447](https://github.com/avianphysics/avian/issues/447)). The tree at `b620b67` has no vehicle example, and crates.io has no maintained Avian vehicle crate. Avian users build one from a ray or shape caster per wheel plus forces on a dynamic body, the pattern the other engines package.

---

## PhysX Vehicle SDK (Vehicle2, PhysX 5)

The SDK behind Unity's `WheelCollider` (whose Manual links the PhysX 4.1 vehicle docs) and Unreal's legacy PhysX vehicles. In 5.1 it was "significantly reworked" into components that "may be mixed, matched and customised" ([Introduction](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Vehicles.html#introduction)).

### Shape of the API

- **A vehicle is data plus a component sequence.** Vehicles are "made of" commands, parameters, states, C functions, components and component sequences ([Software Architecture](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Vehicles.html#overview)). A component is a `PxVehicleComponent` with `update(dt, context)` and a virtual function that gathers its data; a `PxVehicleComponentSequence` runs them in order, with nestable sub-step groups for "the numerically stiffest parts" ([Components](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Vehicles.html#components), [Component Sequences](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Vehicles.html#component-sequences)).
- **The SDK imposes no data layout.** Per-wheel data may be struct-of-arrays or array-of-structs, passed as `PxVehicleArrayData` ([Parameter and State Data Layout](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Vehicles.html#parameter-and-state-data-layout)). The snippets make one class per vehicle that inherits every component it needs ([Vehicle Design Pattern](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Vehicles.html#vehicle-design-pattern)).
- **The vehicle has its own rigid body,** mirrored into a `PxRigidDynamic` or articulation link: a begin component reads the actor's state, an end component writes it back, and the actor must have gravity disabled so it is not applied twice ([PhysX Scene Integration](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Vehicles.html#physx-scene-integration), [PhysX Actor](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Vehicles.html#physx-actor)). The snippet's order is set commands, `vehicle.step`, then `scene->simulate` ([SnippetVehicleDirectDrive.cpp L299–L311](https://github.com/NVIDIA-Omniverse/PhysX/blob/5ca9f472105a90d70d957c243cb0ef36fe251a9f/physx/snippets/snippetvehicledirectdrive/SnippetVehicleDirectDrive.cpp#L299-L311)).
- **Wheel positions** are suspension frames relative to the rigid body's centre of mass ([Mechanical Model](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Vehicles.html#mechanical-model)).

### Kinds

**One component set, several drive models.** Direct drive, four-wheel engine drive, multi-wheel drive and tank drive; the legacy `PxVehicleNoDrive`, `PxVehicleDrive4W`, `PxVehicleDriveNW` and `PxVehicleDriveTank` are recreated from components ([Legacy Vehicle Types](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Vehicles.html#legacy-vehicle-types)). A tank has "two thrust levers and two brake levers", split between tracks by `PxVehicleTankDriveDifferentialComponent` ([SnippetVehicleTankDrive](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Vehicles.html#snippetvehicletankdrive)). The guide does not cover motorcycles.

### Driver input

- **Normalised commands, as plain structs written before each step.** `PxVehicleCommandState { brakes[2], nbBrakes, throttle, steer }`: brakes and throttle in [0, 1], steer in [−1, 1] ([PxVehicleCommandStates.h L49–L54](https://github.com/NVIDIA-Omniverse/PhysX/blob/5ca9f472105a90d70d957c243cb0ef36fe251a9f/physx/include/vehicle2/commands/PxVehicleCommandStates.h#L49-L54)). The two brakes can be brake and handbrake, or a tank's left and right levers ([Commands](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Vehicles.html#commands)).
- **Transmission commands per drive type:** a direct-drive gear (forward, reverse, neutral); an engine-drive `clutch` and `targetGear`; a tank adds thrusts ([PxVehicleCommandStates.h L65–L116](https://github.com/NVIDIA-Omniverse/PhysX/blob/5ca9f472105a90d70d957c243cb0ef36fe251a9f/physx/include/vehicle2/commands/PxVehicleCommandStates.h#L65-L116)).
- Command response parameters turn pedals into torques and angles per wheel ([Tuning Guide](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Vehicles.html#tuning-guide)).

### Drivetrain and tuning

The fullest set surveyed: brake, steer and Ackermann response; wheel; suspension, suspension compliance (toe, camber, force application points); suspension force; anti-roll bars; tyre force (stiffnesses, `frictionVsSlip`, `loadFilter`); rigid body; engine (torque curve, inertia, damping), gearbox, autobox, clutch, and multi-wheel, four-wheel and tank differentials ([Tuning Guide](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Vehicles.html#tuning-guide)). The default engine "will drive a standard car of around 1500kg" ([Troubleshooting](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Vehicles.html#troubleshooting)). Vehicles can also be loaded from JSON data (the snippets' `--vehicleDataPath`).

### Read-back

**States are public structs the caller owns**, updated each step: suspension `jounce` (distance from droop), `jounceSpeed` and `separation` ([PxVehicleSuspensionStates.h L49–L70](https://github.com/NVIDIA-Omniverse/PhysX/blob/5ca9f472105a90d70d957c243cb0ef36fe251a9f/physx/include/vehicle2/suspension/PxVehicleSuspensionStates.h#L49-L70)), wheel `rotationSpeed` ([PxVehicleWheelStates.h L57–L64](https://github.com/NVIDIA-Omniverse/PhysX/blob/5ca9f472105a90d70d957c243cb0ef36fe251a9f/physx/include/vehicle2/wheel/PxVehicleWheelStates.h#L57-L64)), tyre slip, grip (load, friction), sticky state and forces ([PxVehicleTireStates.h L48–L144](https://github.com/NVIDIA-Omniverse/PhysX/blob/5ca9f472105a90d70d957c243cb0ef36fe251a9f/physx/include/vehicle2/tire/PxVehicleTireStates.h#L48-L144)), engine and gearbox state ([PxVehicleDrivetrainStates.h](https://github.com/NVIDIA-Omniverse/PhysX/blob/5ca9f472105a90d70d957c243cb0ef36fe251a9f/physx/include/vehicle2/drivetrain/PxVehicleDrivetrainStates.h)), and the ground under each wheel as `PxVehicleRoadGeometryState { plane, friction, velocity, hitState }` ([PxVehicleRoadGeometryState.h L42–L47](https://github.com/NVIDIA-Omniverse/PhysX/blob/5ca9f472105a90d70d957c243cb0ef36fe251a9f/physx/include/vehicle2/roadGeometry/PxVehicleRoadGeometryState.h#L42-L47)).

### Surfaces

**A material-to-friction lookup table.** `PxVehiclePhysXMaterialFrictionParams` maps `PxMaterial` pointers to friction values, with a `defaultFriction` for unknown materials ([PxVehiclePhysXRoadGeometryParams.h L121–L168](https://github.com/NVIDIA-Omniverse/PhysX/blob/5ca9f472105a90d70d957c243cb0ef36fe251a9f/physx/include/vehicle2/physxRoadGeometry/PxVehiclePhysXRoadGeometryParams.h#L121-L168)). The tyre's final friction is that value times the `frictionVsSlip` graph ([PxVehicleTireForceParams](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Vehicles.html#pxvehicletireforceparams)). The scene-query component "may be replaced by any component that can compute a plane and a friction value for each wheel" ([PxVehiclePhysXRoadGeometrySceneQueryComponent](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Vehicles.html#pxvehiclephysxroadgeometryscenequerycomponent)).

### Wheel visuals

**A helper, not a pose write.** `PxVehicleComputeWheelLocalPose` and `PxVehicleComputeWheelPose` build a wheel's pose from suspension state, camber, toe, steer and rotation angle ([PxVehicleWheelHelpers.h L95–L175](https://github.com/NVIDIA-Omniverse/PhysX/blob/5ca9f472105a90d70d957c243cb0ef36fe251a9f/physx/include/vehicle2/wheel/PxVehicleWheelHelpers.h#L95-L175)). The PhysX actor may carry one shape per wheel, which the end component poses ([PxVehiclePhysXActorStates.h L51–L60](https://github.com/NVIDIA-Omniverse/PhysX/blob/5ca9f472105a90d70d957c243cb0ef36fe251a9f/physx/include/vehicle2/physxActor/PxVehiclePhysXActorStates.h#L51-L60)). The wheel radius must match the render mesh, or wheels hover or sink ([PxVehicleWheelParams](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Vehicles.html#pxvehiclewheelparams)).

### Other bodies and characters

- **What wheels see is a query filter:** `PxVehiclePhysXRoadGeometryQueryParams` holds default filter data, optional per-wheel filter data, a filter callback, and raycast or sweep ([PxVehiclePhysXRoadGeometryParams.h L63–L119](https://github.com/NVIDIA-Omniverse/PhysX/blob/5ca9f472105a90d70d957c243cb0ef36fe251a9f/physx/include/vehicle2/physxRoadGeometry/PxVehiclePhysXRoadGeometryParams.h#L63-L119)). The snippets query static geometry only ([PhysXIntegration.cpp L117](https://github.com/NVIDIA-Omniverse/PhysX/blob/5ca9f472105a90d70d957c243cb0ef36fe251a9f/physx/snippets/snippetvehiclecommon/physxintegration/PhysXIntegration.cpp#L117)), and their scene filter shader suppresses every contact, so the vehicles there are held up only by suspension ([SnippetVehicleHelpers.cpp L41–L54](https://github.com/NVIDIA-Omniverse/PhysX/blob/5ca9f472105a90d70d957c243cb0ef36fe251a9f/physx/snippets/snippetvehiclecommon/SnippetVehicleHelpers.cpp#L41-L54)). A driver is kept off the wheels by its filter data.
- **Moving ground:** the road state carries the ground's `velocity` (above).
- **Trailers are ordinary joints between two vehicles' actors.** SnippetVehicleTruck joins a tractor and trailer with a D6 joint, swing free, collision enabled ([SnippetVehicleTruck.cpp L306–L313](https://github.com/NVIDIA-Omniverse/PhysX/blob/5ca9f472105a90d70d957c243cb0ef36fe251a9f/physx/snippets/snippetvehicletruck/SnippetVehicleTruck.cpp#L306-L313)). The trailer has "no drivetrain and no steering column", so its wheels respond only to brakes ([SnippetVehicleTruck](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Vehicles.html#snippetvehicletruck)).

### Scripting access

C++ only; engines wrap it.

### Complaints and the SDK's own answers

- **Low-speed instability is acknowledged:** "the tire model has a tendency towards instability and oscillation at low speed". The answer is a "sticky tire" state that swaps tyre forces for velocity constraints on the PhysX actor once speed stays below a threshold; without an actor, small timesteps or sub-steps instead ([PxVehiclePhysXConstraintComponent](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Vehicles.html#pxvehiclephysxconstraintcomponent)).
- **Wheels sinking:** when the ground can be reached only beyond maximum compression, the same constraint pushes the vehicle up; without it "the wheel will remain below the ground plane" (same section).
- **Bouncing:** `loadFilter` exists because large timesteps exaggerate spring motion and tyre load ([PxVehicleTireForceParams](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Vehicles.html#pxvehicletireforceparams)).
- The troubleshooting list starts with "Jittery Vehicles" (length scale, spring frequency against timestep) and covers engines that will not spin, wheels that spin but do not move, no steering, sluggish acceleration, too much wheelspin, and climbing slopes that are too steep ([Troubleshooting](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Vehicles.html#troubleshooting)).

---

## Comparison

| | Unity `WheelCollider` | Unity ECS Vehicles (exp.) | Unreal Chaos | Godot `VehicleBody3D` | O3DE | Rapier / bevy_rapier | Avian | PhysX Vehicle2 |
|---|---|---|---|---|---|---|---|---|
| **Vehicle is** | Implicit: wheels on one `Rigidbody` | Components on the chassis entity | Movement component on a pawn | `RigidBody3D` subclass | None (robotics gem: jointed bodies; PR: API) | Controller object; bevy: component on chassis | None | Data plus component sequence |
| **Wheels live** | A `Collider` per GameObject | Wheel entities, plus a buffer on the vehicle | Array of setups (class and bone) | Child nodes | Entities on joints | Rapier: private `Vec`; bevy: `Vec` field | — | Caller's arrays |
| **Wheel position** | Transform plus `center` | Wheel entity transform | Bone plus offset | Node transform | Joint frames | Chassis-local vectors | — | Suspension frames relative to centre of mass |
| **Ground query** | Ray | Cylinder collider cast | Ray (default) or sweep | Ray | Physical contact | Ray | — | Ray or sweep |
| **Kinds** | Cars | Cars, trucks, trailers | Any wheel count; arcade helpers; no tracks | Cars | Ackermann, skid steer | Any wheel count | — | Cars, multi-wheel, tanks |
| **Driver input** | Per-wheel torque (N·m), steer (degrees) | Normalised fields, gear events | Normalised setter calls, gears, handbrake, rates | Force (N) and steer (radians), body or wheel | Target speeds over an EBus, expire after 200 ms | Per-wheel force, brake, steer | — | Normalised command structs, gear and clutch |
| **Drivetrain** | None | Engine curve, automatic gearbox, final ratio, aero | Engine curve, gearbox, differential, aero | None | None (PID on joint motors) | None | — | Engine, gearbox, autobox, clutch, differentials |
| **Anti-roll** | None | Per axle pair | `RollbarScaling` | `roll_influence` only | — | `roll_influence` only | — | Anti-roll bars |
| **Read-back** | Queried: grounded, rpm, `WheelHit` with slip | Written to buffer: contact, hit, compression, slip, impulses, rpm, gear | Queried: speed, rpm, gear; `FWheelStatus` per wheel | Queried: contact, normal, body, rpm, skid | Odometry only | Rapier: queried; bevy: written into the component | — | Caller-owned state structs |
| **Surfaces** | Game code swaps friction curves | Per-wheel curve only | Surface friction coefficient; material reported; runtime multiplier | Per-wheel slip value (docs claim surface is combined; code does not) | Physics materials | None | — | Material-to-friction table |
| **Wheel visuals** | `GetWorldPose`, game code copies | Systems pose spin and wheel entities | Animation node on bones | Engine writes the wheel node | Bodies are the wheels | bevy: `local_transform()` helper | — | Pose helper; actor wheel shapes |
| **Driver kept off wheels by** | Layers (wheel layer overrides) | Self-hit skipped; filters | Wheel trace channel responses | Collision mask only | Collision layers | Exclude sets, groups, `ControllerIgnored` | — | Query filter data and callback |
| **Pushes ground body** | Not documented | Deferred impulses | Not documented | No (TODO) | Physically | No (TODO) | — | Through the actor |
| **Parked creep answer** | `forceAppPointDistance` advice | Anti-creep reference | Sleep threshold and slope limit; park | Open PR | — | — | — | Sticky tyre constraints |
| **Scripting** | C# | C# | Blueprint, C++ | GDScript, C# | C++ EBus, ROS 2 | Rust, JS, Python, C | — | C++ |
| **Common complaints** | Kerb pop; jitter at rest; tuning; flips launch | Slides or jitters at rest | Hard to tune outside intent; no tracks | Slides when parked; below-60 Hz breakage; freeze; defaults | Absence | Braking friction; wheel spin read-back | No vehicle | Low-speed oscillation; sinking; bounce |

---

## What this means for FenrirPhysics

FenrirPhysics keeps plain-data components that physics diffs each tick, and an entity holds at most one component of each type (ADR-0011). C# native functions may read and write component data but make no structural changes (ADR-0009). Joints are joint entities with explicit local frames (ADR-0013); characters are a velocity field physics applies in its own phase (ADR-0014). Against those rules, the surveyed engines leave these options for the vehicle tickets that follow. What Jolt can back is ticket 40's answer.

**Where the vehicle and its wheels live**

- **A. One vehicle component on the chassis entity, with wheels as an array in it** (bevy_rapier's `RayCastVehicleController.wheels`, Unreal's `WheelSetups`, PhysX's arrays, the O3DE proposal).
  - One component per entity is respected; adding a wheel is a field edit, not a structural change.
  - Wheel positions are chassis-local values, as ADR-0013's frames are.
  - Settings, inputs and read-back share one component unless split (see below). A variable-length array inside a plain-data component is a question for the C# side.
- **B. A wheel entity per wheel, pointing at its vehicle** (Unity's ECS package and sample, Godot's child nodes, Unity's `WheelCollider` GameObjects).
  - The wheel's transform is its position, and the wheel entity is a natural parent for the wheel mesh.
  - Adding or removing a wheel is a structural change, which C# native functions cannot make. Unity's package then needs wheel-added and wheel-removed events and a vehicle-side buffer to keep order.
  - It repeats ADR-0013's shape: an entity that names the body it belongs to.
- **C. Wheels as rigid bodies on joint entities** (O3DE's robotics gem, Rapier's joint example). It needs no vehicle type at all, but Rapier's guide calls it "difficult to control", and none of the game engines take this path for cars.

**How game code drives it**

- **Normalised fields written each tick and read by physics in its own phase** (Unity's ECS `VehicleControl`, PhysX's `PxVehicleCommandState`, bevy_rapier's per-wheel fields). It is ADR-0014's pattern again, and needs no native call.
- **Setter calls** (Unreal, the O3DE proposal) are the same data behind functions. Unreal's input rates, `bReverseAsBrake` and "requires controller" show how much input shaping engines put between game code and the pedals.
- **Physical versus normalised:** per-wheel torque or force (Unity `WheelCollider`, Godot, Rapier) needs no drivetrain but makes game code tune per vehicle mass. Pedals in [0, 1] and steer in [−1, 1] (Unreal, PhysX, Unity ECS) need a drivetrain or response parameters behind them.
- **Gears and engine start** are edges, not levels: Unity's ECS package uses both booleans and an event buffer; Unreal uses up and down calls. Under ADR-0012, they could be events or fields that physics clears.

**Drivetrain depth.** Three levels appear: none (Godot, Rapier, Unity `WheelCollider`); engine, gearbox and differential behind pedals (Unreal, Unity ECS); and fully replaceable components (PhysX). The engines with a drivetrain also carry the most "vehicle doesn't move" troubleshooting. Unity's ECS defaults (an engine that starts on awake, an automatic box, 12 sub-steps) are the clearest "drives out of the box" set.

**Read-back.** Written back into a component each tick (bevy_rapier's `state`, Unity ECS's buffer) or queried through calls (Unity, Unreal, Godot). The common per-wheel set is in-contact, contact point and normal, ground body, suspension compression, slip, and spin; per vehicle, forward speed, engine RPM and gear. Unity's package splits settings (`Wheel`) from state (`WheelOnVehicle`); bevy_rapier keeps them together.

**Surfaces.** Per-wheel friction that ignores the ground (Unity, Godot, Rapier), a reported ground material that game code acts on (Unity, Unreal), or a material-to-friction table the vehicle reads (PhysX). FenrirPhysics already has a `PhysicsMaterial` per collider, which a table could key on.

**Wheel visuals.** The engine poses a node it owns (Godot, Unity ECS), the engine offers a pose for game code to copy (Unity's `GetWorldPose`, bevy_rapier's `local_transform()`, PhysX's helper), or an animation system does it (Unreal). With option B the wheel entity's `Transform` is the obvious target; with option A physics would need a named entity per wheel, or game code copies a pose.

**Other bodies and the driver.**

- **The chassis is excluded from its own wheel queries everywhere.** Beyond it, engines filter wheels by collision mask (Godot), groups and exclude lists (bevy_rapier, PhysX), or separate trace responses (Unreal). Godot shows the trap: its collision exceptions do not reach the wheel rays.
- **Pushing the ground body back** is missing in the two Bullet ports (Godot and Rapier both leave a TODO); Unity's sample and package apply equal and opposite impulses.
- **Trailers are joints everywhere** (PhysX's truck snippet's D6 joint, the `ConfigurableJoint` in Unity's truck-and-trailer prefab), which ADR-0013 already covers.
- **No engine has an enter or exit concept** below the gameplay layer; Unreal's possession is the nearest. Seating a character is attaching it and filtering its collision against the chassis and wheels.

**Parked creep and low-speed jitter.** Every engine with a tyre model has an answer: an anti-creep anchor (Unity ECS), sticky-tyre constraints (PhysX), aggressive sleep (Unreal), or advice and an open fix (Unity's `forceAppPointDistance`, Godot's PR). It is the most common complaint and the place a "drives out of the box" default most needs care.

**Scripting.** Every option above is field data, so the C# surface would mirror the components. The gaps to avoid are O3DE's robotics gem (an EBus only) and PhysX's (C++ structs only).
