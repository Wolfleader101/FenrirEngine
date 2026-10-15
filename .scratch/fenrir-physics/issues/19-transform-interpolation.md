# Is there transform interpolation, and who owns it?

Type: grilling
Status: open
Blocked by: 05

## Question

Physics steps once per fixed tick in `PostTick` ("How does physics step?"), but the DemoApp renders in `Update` at frame rate. At 144 Hz rendering with a 60 Hz tick, bodies visibly stutter without interpolation. `Time` already gives the fraction `accumulator / tickRate`.

- Is interpolation in the foundation spec, opt-in per body (a tag such as Avian's `TransformInterpolation`), or engine-wide?
- Where the interpolated pose goes:
  - written into `Transform` between steps and restored before physics's diff (Avian's approach; the diff must not take it for a game edit, and C# and gameplay systems in `Update` would read the interpolated pose);
  - a separate value the renderer reads (a rendered world matrix), which changes what renderers read and so is an engine question rather than a physics one.
- Which phase writes it, relative to the fixed loop and rendering (the renderer runs in `Update`), and whether that needs a new phase.
- Interpolation (between the last two steps, one step behind) or extrapolation (ahead from the last step using velocity).
- Kinematic bodies moved by game code in `Tick`, and teleports (no interpolation across a teleport).
