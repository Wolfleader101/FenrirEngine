# 05: Teleports, rebuilds and wheels lost mid-drive

**What to build:** Respawning a vehicle is a `Transform` write, changes during play feel continuous, and losing a wheel mid-drive is never a crash (spec: "the step in `PostTick`" (teleports, rebuilds, the drain's chassis rebuild), "edges" (chassis shape, scale, wheels destroyed mid-drive)).

- **Teleports:** a chassis teleport is the existing body teleport. Engine RPM and wheel spin carry over, as a body's velocity does; wheel poses snap.
- **Rebuilds carry over** what still maps: engine RPM, gear, and each surviving wheel's spin. New wheels start still, the chassis keeps its velocity, wheel poses snap. This ticket builds the carry-over and proves it for a wheel-set change (adding, removing or relinking a wheel) and a chassis body rebuilt in one drain (a collider swapped, or its `RigidBody` removed and added), which removes and recreates the vehicle. Tickets 07–09 reuse it for kind changes and `MotorcycleLean::maxAngle`.
- **Chassis scale:** a world-scale change patches wheel positions in place as a live wheel edit, with no rebuild, and wakes the chassis. A shape or centre-of-mass change needs nothing.
- **Dead names:** a `VehicleDifferential` or `VehicleAntiRollBar` treats a wheel name that has died (entity destroyed, or its `VehicleWheel` removed) as empty, with one warning, and is not made inactive. A differential left with one wheel drives it; an anti-roll bar left with one wheel is inert; a differential left with no wheels names no vehicle, so if it was that vehicle's only one, the vehicle falls back to all-wheel drive (the stated limit, documented).
- **DemoApp (C++):** T teleports the driven vehicle to the start; 8 destroys and remakes the car's rear-left wheel (a rebuild each time, one dead-name warning from the rear differential). Smoke keys extended; 8's warning is one of the two expected ones.

**Blocked by:** 01 (Vehicle tracer bullet); 04 (Differentials and anti-roll bars).

**Status:** resolved

- [x] `FenrirPhysicsTests`: a chassis teleport keeps RPM and spin and snaps wheel poses; adding a wheel mid-drive keeps RPM, gear and the surviving wheels' spin, the new wheel starting still; a chassis whose collider is swapped in one drain carries over the same way.
- [x] A chassis scale change moves wheel positions with no rebuild (RPM and spin untouched) and wakes the chassis.
- [x] A destroyed wheel rebuilds the vehicle; a rear differential with one dead wheel drives the other with one warning; an anti-roll bar with one dead wheel is inert; a differential with no wheels left lets its vehicle fall back to all-wheel drive.
- [x] `FENRIR_DEMO_SCENE="Vehicles (C++)"` with `FENRIR_DEMO_KEYS=1` runs with no errors, in Debug and Release, 8's warning the only one.
- [ ] 8 gives one warning and the car stays rear-wheel drive on its one rear wheel; 8 again restores it; T respawns the car (checked by hand).

**Resolved by:** PR #56 (branch claude/vibrant-morse-e72253), into dotnet-scripting. The proof is `FenrirPhysicsTests`' `[vehicles][rebuilds]` cases (`Tests/VehicleRebuilds.cpp`) and the `FENRIR_DEMO_SCENE="Vehicles (C++)" FENRIR_DEMO_KEYS=1` smoke run, which taps T and 8 twice each; the last box (watching the car by hand) is left for a person. Choices the ticket left open:
- A teleport needed no new code: the backend vehicle is untouched by the body teleport, so its engine and wheels carry on, and wheels are posed each tick on the chassis's step pose, so they snap. The tests prove both.
- The carry-over is read when the backend vehicle is dropped for a rebuild (`DropVehicle(record, true)` in `src/Vehicles.cpp`: a wheel-set change in the drain, the chassis's body rebuilt, or a changed wheel list or body in step 2) and kept on the record by wheel entity; the next `CreateVehicle` takes it as `VehicleDesc::carried` (engine RPM, gear, clutch, and each wheel's angular velocity and spin angle). Dropping for disabled, inactive or wheel-less forgets it, so those still start fresh. The gear is kept within the gear count.
- Rebuild tests drive the car with a differential on each pair of wheels at 0.25 throttle: a rear-wheel-drive car at that throttle slips and grips its rear tyres on alternate ticks, so one tick's spin is not a fair compare. A wheel added on the roof, in no differential, shows the new wheel starting still.
- A dead name is a handle whose entity is gone, or an entity that lost its VehicleWheel after the link had seen it as a wheel (`VehicleLinkRecord::wheels`); a never-wheel entity is still a wrong name, inactive. Each dead name warns once, until it is renamed or comes back.
- The DemoApp's 8 also clears the rear anti-roll bar's name to the destroyed wheel (game code tidying a bar that is inert with one wheel anyway), so the rear differential's dead-name warning is 8's only one. The remade wheel copies the rear-right wheel's settings mirrored, so 1 and 2 still apply. T also zeroes the chassis's velocity, as a respawn.
