# 13: Vehicles debug lines

**What to build:** Any renderer can show each wheel's suspension, its circle and its grip through a new debug-line category (spec: "FenrirPhysics: debug lines", "DemoApp" (the F2 cycle)). `DebugDraw::Vehicles = 1u << 6` is included in `All`; the C# mirror lands with ticket 15. For each wheel of each active, enabled vehicle, at step poses:

- **the suspension:** a line from the top of the travel to the full-extension point (`position`), with a short cross-tick at the current wheel centre;
- **the wheel:** a circle of its radius in the wheel's plane, with one spoke so spin and steer show;
- **the contact, while on ground:** a cross at the point and a line along the normal, coloured by grip: gripping, past `peakSlip`, past `slideSlip`.

The suspension, wheel and three grip colours are new `DebugColour`s. Probe shapes are not drawn. Disabled and inactive vehicles draw nothing; `Colliders` still draws the chassis. Lines come from the backend's public getters, never its compiled-out drawing. The DemoApp's F2 cycle gains "vehicles" before "all".

**Blocked by:** 03 (Wheel settings, probe and tilt limit).

**Status:** ready-for-agent

- [ ] `FenrirPhysicsTests`: a known four-wheeled vehicle draws the expected line counts; the contact colour changes from gripping to past `peakSlip` to past `slideSlip` as slip grows; disabled and inactive vehicles draw nothing; `All` includes `Vehicles`.
- [ ] `FENRIR_DEMO_SCENE="Vehicles (C++)"` with `FENRIR_DEMO_KEYS=1` runs with no errors, in Debug and Release.
- [ ] F2's "vehicles" mode draws the suspension, the wheel circles and the contacts coloured by slip, and the mode carries across an F6 switch (checked by hand).
