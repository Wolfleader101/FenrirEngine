# 13: Vehicles debug lines

**What to build:** Any renderer can show each wheel's suspension, its circle and its grip through a new debug-line category (spec: "FenrirPhysics: debug lines", "DemoApp" (the F2 cycle)). `DebugDraw::Vehicles = 1u << 6` is included in `All`; the C# mirror lands with ticket 15. For each wheel of each active, enabled vehicle, at step poses:

- **the suspension:** a line from the top of the travel to the full-extension point (`position`), with a short cross-tick at the current wheel centre;
- **the wheel:** a circle of its radius in the wheel's plane, with one spoke so spin and steer show;
- **the contact, while on ground:** a cross at the point and a line along the normal, coloured by grip: gripping, past `peakSlip`, past `slideSlip`.

The suspension, wheel and three grip colours are new `DebugColour`s. Probe shapes are not drawn. Disabled and inactive vehicles draw nothing; `Colliders` still draws the chassis. Lines come from the backend's public getters, never its compiled-out drawing. The DemoApp's F2 cycle gains "vehicles" before "all".

**Blocked by:** 03 (Wheel settings, probe and tilt limit).

**Status:** resolved

- [x] `FenrirPhysicsTests`: a known four-wheeled vehicle draws the expected line counts; the contact colour changes from gripping to past `peakSlip` to past `slideSlip` as slip grows; disabled and inactive vehicles draw nothing; `All` includes `Vehicles`.
- [x] `FENRIR_DEMO_SCENE="Vehicles (C++)"` with `FENRIR_DEMO_KEYS=1` runs with no errors, in Debug and Release.
- [ ] F2's "vehicles" mode draws the suspension, the wheel circles and the contacts coloured by slip, and the mode carries across an F6 switch (checked by hand).

**Resolved by:** PR #57 (branch claude/frosty-pascal-bcff19), stacked on #56 (ticket 05). The proof is `FenrirPhysicsTests`' `[debug][vehicles]` cases (`Tests/VehicleDebugLines.cpp`) and the `FENRIR_DEMO_SCENE="Vehicles (C++)" FENRIR_DEMO_KEYS=1` smoke run, which now presses F2 seven times first so it runs with "vehicles" drawn; the last box (watching the lines by hand, and across F6) is left for a person. Choices the ticket left open:
- Per wheel: the suspension is 3 lines (the travel, and a two-line cross-tick in the mount's X and Z at the wheel's centre); the wheel is 33 (a 32-segment circle in its Y-Z plane and a spoke up its Y); the contact 4 (a three-axis cross and the normal, the sizes Contacts uses).
- The grip colour is the worse of the two slips: PastSlideSlip if either slip is past its tyre's `slideSlip`, else PastPeakSlip if either is past its `peakSlip`, else Gripping. Slips are compared by magnitude, against the settings as corrected.
- The new colours are `DebugColour::Suspension`, `Wheel`, `Gripping`, `PastPeakSlip` and `PastSlideSlip`.
- C#'s `DebugDraw.All` gains the bit (`1u << 6`), so it stays C++'s `All` (`FenrirPhysicsScripting` checks that); the named `DebugDraw.Vehicles` is still ticket 15's.
- Jolt's slips are noisy while a car is at rest or pulling away (they swing on alternate ticks), so a parked car's contacts may flicker; the lines show what the read-back says.
