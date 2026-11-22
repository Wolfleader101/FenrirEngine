# 03: PhysicsDemo's vehicles use the engine's dead zone

Spec: [Stick and trigger dead zones](../spec.md).

**What to build:** PhysicsDemo's vehicle scenes, the C++ ones and the C# one, drop their own 0.15 dead zones and drive on the engine's dead-zoned values, their comments saying so.

**Blocked by:** 01
**Touches:** `examples/PhysicsDemo/src/`, `examples/PhysicsDemo/scripts/`

**Status:** ready-for-agent

Beside 02, on 01.

- [ ] The vehicle scenes hold no dead zone of their own.
- [ ] Driving from a gamepad works as before (a hand check); PhysicsDemo's smoke runs and per-scene log checks pass.
