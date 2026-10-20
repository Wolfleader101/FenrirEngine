# 04: Differentials and anti-roll bars

**What to build:** Game code chooses front-, rear- or all-wheel drive and torque splits, and stops a car leaning in corners, with components naming two wheel entities and no arrays (spec: "FenrirPhysics: the components" (`VehicleDifferential`, `VehicleAntiRollBar`, inactive), "settings and defaults" (drives out of the box, differentials), "the step in `PostTick`" (drain and diff), "the Jolt backend" (built from the shape fields)).

- **`VehicleDifferential`** (on any entity: `leftWheel`, `rightWheel`, `ratio`, `leftRightSplit`, `limitedSlipRatio`, `torqueShare`) and **`VehicleAntiRollBar`** (`leftWheel`, `rightWheel`, `stiffness`), declared and documented with the spec's defaults. Their watches drain with the others; edits and relinks are live and wake the chassis.
- **Drives out of the box, replaced:** a wheeled vehicle with no differential naming its wheels keeps ticket 01's all-wheel-drive default (one single-wheel differential per wheel with `VehicleDifferential`'s defaults). Adding any differential replaces that default; a wheel in no differential only rolls and brakes. An empty wheel name is "none", so a rear-only drive is one differential with one wheel. Torque shares are normalised privately per vehicle.
- **No anti-roll bars by default;** `VehicleAntiRollBar` works on any kind with wheels. Tracked and trailer vehicles will ignore differentials (tickets 08 and 09).
- **Inactive** (component kept, no effect, one warning on each transition to invalid, back once valid): a name that is a non-wheel entity, or wheels of two different vehicles. Names that *die* (the entity destroyed or its `VehicleWheel` removed) are ticket 05's.
- **Out of range,** one warning per component: negative stiffness treated as 0; a limited-slip ratio at or below 1 raised to just above it.
- **DemoApp (C++):** the car gets a rear `VehicleDifferential` and front and rear `VehicleAntiRollBar`s.

**Blocked by:** 01 (Vehicle tracer bullet).

**Status:** ready-for-agent

- [ ] `FenrirPhysicsTests`: with no differential every wheel is driven; a rear `VehicleDifferential` drives only its two wheels (the front wheels' spin follows the ground); a one-wheel differential drives one wheel; two differentials with shares 3 and 1 split torque as 0.75 and 0.25.
- [ ] An anti-roll bar reduces the chassis's roll in a steady turn against the same car without one.
- [ ] A differential or anti-roll bar naming a non-wheel or wheels of two vehicles is inactive with one warning and comes back once its names are fixed.
- [ ] Out-of-range values corrected with one warning; edits wake a sleeping chassis.
- [ ] `FENRIR_DEMO_SCENE="Vehicles (C++)"` with `FENRIR_DEMO_KEYS=1` runs with no errors, in Debug and Release.
