# 03: F11 toggles fullscreen in every demo app

Spec: [Borderless fullscreen](../spec.md).

**What to build:** The demo scaffold gains a shared F11 key that toggles the main window between windowed and borderless fullscreen, listed in its help with the scaffold's other keys and pressed by no smoke run.

**Blocked by:** 01
**Touches:** `examples/DemoScaffold/`

**Status:** ready-for-agent

Beside 02, on 01. The pause spec's demo-keys ticket (`.scratch/fenrir-time-scale/issues/06`) also adds scaffold keys: whichever lands second is built on the other.

- [ ] F11 toggles fullscreen and back to the same window in every demo app (a hand check).
- [ ] The scaffold's help lists F11.
- [ ] Every demo app's smoke runs pass.
