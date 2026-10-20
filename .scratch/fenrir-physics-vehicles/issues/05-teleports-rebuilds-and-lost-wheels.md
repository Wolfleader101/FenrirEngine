# 05: Teleports, rebuilds and wheels lost mid-drive

**What to build:** Respawning a vehicle is a `Transform` write, changes during play feel continuous, and losing a wheel mid-drive is never a crash (spec: "the step in `PostTick`" (teleports, rebuilds, the drain's chassis rebuild), "edges" (chassis shape, scale, wheels destroyed mid-drive)).

- **Teleports:** a chassis teleport is the existing body teleport. Engine RPM and wheel spin carry over, as a body's velocity does; wheel poses snap.
- **Rebuilds carry over** what still maps: engine RPM, gear, and each surviving wheel's spin. New wheels start still, the chassis keeps its velocity, wheel poses snap. This ticket builds the carry-over and proves it for a wheel-set change (adding, removing or relinking a wheel) and a chassis body rebuilt in one drain (a collider swapped, or its `RigidBody` removed and added), which removes and recreates the vehicle. Tickets 07–09 reuse it for kind changes and `MotorcycleLean::maxAngle`.
- **Chassis scale:** a world-scale change patches wheel positions in place as a live wheel edit, with no rebuild, and wakes the chassis. A shape or centre-of-mass change needs nothing.
- **Dead names:** a `VehicleDifferential` or `VehicleAntiRollBar` treats a wheel name that has died (entity destroyed, or its `VehicleWheel` removed) as empty, with one warning, and is not made inactive. A differential left with one wheel drives it; an anti-roll bar left with one wheel is inert; a differential left with no wheels names no vehicle, so if it was that vehicle's only one, the vehicle falls back to all-wheel drive (the stated limit, documented).
- **DemoApp (C++):** T teleports the driven vehicle to the start; 8 destroys and remakes the car's rear-left wheel (a rebuild each time, one dead-name warning from the rear differential). Smoke keys extended; 8's warning is one of the two expected ones.

**Blocked by:** 01 (Vehicle tracer bullet); 04 (Differentials and anti-roll bars).

**Status:** ready-for-agent

- [ ] `FenrirPhysicsTests`: a chassis teleport keeps RPM and spin and snaps wheel poses; adding a wheel mid-drive keeps RPM, gear and the surviving wheels' spin, the new wheel starting still; a chassis whose collider is swapped in one drain carries over the same way.
- [ ] A chassis scale change moves wheel positions with no rebuild (RPM and spin untouched) and wakes the chassis.
- [ ] A destroyed wheel rebuilds the vehicle; a rear differential with one dead wheel drives the other with one warning; an anti-roll bar with one dead wheel is inert; a differential with no wheels left lets its vehicle fall back to all-wheel drive.
- [ ] `FENRIR_DEMO_SCENE="Vehicles (C++)"` with `FENRIR_DEMO_KEYS=1` runs with no errors, in Debug and Release, 8's warning the only one.
- [ ] 8 gives one warning and the car stays rear-wheel drive on its one rear wheel; 8 again restores it; T respawns the car (checked by hand).
