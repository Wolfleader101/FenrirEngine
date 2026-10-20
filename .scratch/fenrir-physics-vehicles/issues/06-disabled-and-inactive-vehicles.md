# 06: Disabled and inactive vehicles

**What to build:** A wreck is one field write, and every misuse of `Vehicle` or `VehicleWheel` is visible with one warning and never crashes (spec: "FenrirPhysics: the components" (inactive, switching), "state" (reset), "the step in `PostTick`" (disabled or inactive), "edges" (the chassis loses its body), "ground, grip and other bodies" (joints)).

- **Disabled** (`enabled = false`): the backend vehicle is removed, leaving a plain rigid body resting on its chassis; wheel entities stay where they last were; `state` on the vehicle and its wheels resets. Re-enabling builds the vehicle fresh with nothing carried over.
- **Inactive vehicle** (component kept, no backend vehicle, one warning on each transition to invalid, back by itself once valid, built fresh): no dynamic `RigidBody` with a usable collider, beside a `CharacterBody`, or a chassis turned kinematic or static or losing its `RigidBody` or usable collider. A car made kinematic for a cutscene restarts at idle in neutral.
- **Inactive wheel:** naming something that is not a vehicle, sitting on the chassis entity, or sharing its entity with a `RigidBody`, `CharacterBody` or any `Collider` (triggers included). It is left out of its vehicle (a rebuild) with one warning.
- **Joints:** a `Joint` naming a wheel entity names a non-body and is inactive under the joints spec's edge rule; nothing new, tested here.
- **No structural changes by physics:** nothing in this ticket adds or removes components.
- **DemoApp (C++):** 4 toggles the car's `enabled` (a plain rolling box when off); 9 makes the car's chassis kinematic and back (one warning, then fresh at idle in neutral). 9's warning is the other expected one.

**Blocked by:** 01 (Vehicle tracer bullet).

**Status:** ready-for-agent

- [ ] `FenrirPhysicsTests`: a disabled vehicle is a plain body, its `state` and its wheels' states reset and its wheel entities left in place; re-enabled, it starts fresh.
- [ ] A vehicle is inactive without a dynamic body, with no usable collider and beside a `CharacterBody`, with one warning per transition and reactivation once valid; a chassis made kinematic and back starts fresh at idle in neutral.
- [ ] A wheel is inactive naming a non-vehicle, on the chassis entity, and beside a `RigidBody`, a `CharacterBody` or a trigger `Collider`, each with one warning.
- [ ] A `Joint` naming a wheel entity is inactive.
- [ ] `FENRIR_DEMO_SCENE="Vehicles (C++)"` with `FENRIR_DEMO_KEYS=1` runs with no errors, in Debug and Release.
- [ ] With 4 off the car is a plain rolling box; 9 gives one warning and on return the car is at idle in neutral (checked by hand).
