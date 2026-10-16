# Is there transform interpolation, and who owns it?

Type: grilling
Status: resolved
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

## Answer

- **In the foundation, on by default:** `RigidBody` gains `bool interpolate = true` (a field, never a structural change), for dynamic and kinematic bodies; static bodies do not move. `PhysicsConfig` can turn it off app-wide.
- **The interpolated pose goes into `Transform`** (Unity, Avian, bevy_rapier). Renderers, the editor and C# need no change; `Update`-phase gameplay reads the pose that is on screen. Rejected: a separate rendered transform, a new engine concept every renderer would have to read.
- **Two new once-per-frame phases** around the fixed loop: `PreUpdate` → `BeforeTicks` → (`PreTick` / `Tick` / `PostTick`) × N → `AfterTicks` → `Update` → `PostUpdate`. They run every frame, even with no tick, and are generic (mirroring Bevy's `BeforeFixedMainLoop` / `AfterFixedMainLoop`; `BeforeTicks` also suits collecting input for the fixed steps).
  - `BeforeTicks`: physics restores each interpolated body's true pose, so `Tick` systems and the diff see real simulation state. If the `Transform` no longer equals the interpolated pose physics wrote, game code changed it during `Update`: that is an edit (a teleport) and the game's value stays.
  - `AfterTicks`: physics writes `lerp(previous step pose, current step pose, accumulator / tickRate)` (slerp for rotation) through `SetWorldTransform`, parents before children.
- **Interpolate, not extrapolate:** always a pose the simulation produced, at one step of visual delay; extrapolation overshoots into walls and pops on contact.
- **Teleports snap:** a teleport (a game edit found by the diff, a `Transform` write during `Update`, a newly created body) sets previous = current, so nothing blends across it. Kinematic bodies moved in `Tick` interpolate like dynamic ones.
