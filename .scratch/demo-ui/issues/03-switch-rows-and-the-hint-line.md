# 03: Switch rows and the hint line

**What to build:** a scene can put every kind of switch LightingDemo's ImGui window has into the demo panel, bound to its own state, and each row explains itself in a hint line.

**Spec:** `.scratch/demo-ui/spec.md`, "The C++ API" (`DemoValue`, the switch rows, logarithmic ranges, hints). ADR-0071's composites also apply.

- **`DemoValue<T>`:** from a `T*`, or from a get and set pair.
  - Each frame in the kit's build stage work, the value is written into its control unless that control is being dragged or edited.
  - A control's Change is written back through the binding the frame it arrives, before the scenes' `Update`.
- **The rows:**
  - `Toggle` over `bool`.
  - `Range`, over `float` with `DemoRangeOptions` (`logarithmic`, `step`, `decimals`, `angle`, which shows degrees over a radian value), and over `int`, with the value shown beside the slider.
  - `Choice` over `int` with option names: buttons for up to four, a previous and next stepper over the name beyond.
  - `Colour` over a linear `Vec3`: a swatch, encoded for display, over hue, saturation and value sliders.
  - `Button` with a callback.
  - `TextEntry` over `std::string`.
- **Logarithmic ranges:** `min * (max / min)^t`. A non-positive `min` is logged as an error and the range is drawn linear.
- **Hints:** every row takes an optional hint. The hint line at the panel's foot shows the hovered row's hint, or else the focused row's, from UI events, and is empty otherwise.
- **Keyboard and text:**
  - a focused text entry claims the keyboard and text input, cancelling the scene's held keys
  - Escape blurs it, and a click on the scene gives the keyboard back
  - F1 types into it rather than hiding the kit
- **README:** DemoScaffold's README documents the rows, the bindings and the hint line.

**Blocked by:** 02

**Touches:** `examples/DemoScaffold/`

**Status:** resolved

- [x] Scaffold tests on the null backend:
  - a toggle's click flips its bound `bool`, and code flipping it moves the control the next frame
  - a range drag changes its value and doesn't turn the free-fly camera; a logarithmic range maps 0, 0.5 and 1 to `min`, `sqrt(min*max)` and `max`
  - a choice of three is three buttons, one of five is a stepper, and both set their index
  - a colour row's sliders set the bound colour, and the swatch follows
  - typing into a text entry sets its string and claims the keyboard (a held W is cancelled); a click on the scene gives the keyboard back
  - the hint line shows a hovered row's hint and clears when the pointer leaves
  - the gamepad's directions with a kit row focused reach the game
- [x] Every demo's `Smoke-null` passes with scripting off and on.
- [ ] Clang and MSVC x64 debug build without new warnings.
