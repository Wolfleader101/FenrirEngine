# 01: Dead zones in FenrirInput

Spec: [Stick and trigger dead zones](../spec.md).

**What to build:** FenrirInput applies dead zones as gamepad axes come through the input feed. Each stick, as the pair of its axes, has a radial dead zone (below its inner value it reads zero; beyond, its length is rescaled to `min((l - i) / (o - i), 1)` along the raw direction); each trigger has a threshold with the same rescale. Left stick, right stick and triggers have their own values per gamepad, starting from engine defaults (0.15 inner for sticks, 0.05 for triggers, outer 1, as starting values) that an app can change for every pad or for one pad by id, and that go when a pad disconnects. `Gamepads::Axis` returns dead-zoned values and a raw read returns what arrived; `GamepadAxisEvent` carries both and is sent only when the dead-zoned value changes. FenrirUI's navigation threshold now reads dead-zoned values.

**Blocked by:** None (can start immediately)
**Touches:** `GLOSSARY.md`, `packages/FenrirInput/include/FenrirInput/`, `packages/FenrirInput/src/`, `packages/FenrirInput/Tests/`, `packages/FenrirInput/api/`, `packages/FenrirUI/Tests/`

**Status:** ready-for-agent

First; 02 and 03 build on it side by side.

- [ ] FenrirInput gamepad tests through the input feed: a resting stick reads zero; a diagonal push keeps its direction; values rescale continuously from zero past the inner value; the outer value reaches full early; trigger thresholds.
- [ ] Per-stick and per-pad settings; defaults for a new pad; settings go with a disconnected pad.
- [ ] Raw reads are unchanged; events carry both values and none are sent for jitter inside the dead zone.
- [ ] A FenrirUI navigation test: a stick resting inside the dead zone does not move focus.
- [ ] The API description is current.
- [ ] GLOSSARY.md gains *Dead zone*: the part of a stick's or trigger's travel near rest that reads as zero, the rest rescaled to still run to full, radially for a stick; *Avoid:* deadband. (A trigger's dead zone is called its threshold.)
