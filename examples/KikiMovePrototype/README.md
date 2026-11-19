# KikiMovePrototype (PROTOTYPE, throwaway)

A grey box for Flightless Frenzy's ticket "How does Kiki move?" (`.scratch/flightless-frenzy/issues/05-how-kiki-moves.md`
on the map branch). It is for judging, by playing, how running, the jump, the extra jump, the glide, the speed boost and
the camera feel, and for measuring the highest ledge and the longest gap Kiki clears. It is not game code. The game is
C# in `games/FlightlessFrenzy` and uses no demo code. This is the fastest thing to write: C++ on the demo scaffold,
driving a FenrirPhysics `CharacterBody` (ADR-0014). Gravity, jumping and everything in `Tuning` is game code in
`src/main.cpp`.

The numbers are starting points, not decisions. Change them while it runs and copy them out with P.

## Build and run

From the worktree root in PowerShell (MSVC, debug; scripting and tests off, which this app does not need):

```powershell
.\scripts\msvc.ps1 cmake --preset msvc-x64-debug -B out/b -DFENRIR_ENABLE_SCRIPTING=OFF -DFENRIR_BUILD_TESTS=OFF
```

```powershell
.\scripts\msvc.ps1 cmake --build out/b --target KikiMovePrototype
```

```powershell
Push-Location out\b\examples\KikiMovePrototype; .\KikiMovePrototype.exe; Pop-Location
```

A headless check: `FENRIR_DEMO_SMOKE=600` with `FENRIR_DEMO_BACKEND=null` runs it for 600 frames and presses its keys,
exiting 0 if nothing logged an error. `KIKI_HOLD_JUMP=1` holds jump down throughout, so the smoke run's tapped Space
gives full jumps. The log then shows each jump's measured rise, for example "peak rise 2.00 m".

## Controls

Click the scene to capture the mouse for looking. Esc frees it, so you can use the demo panel. F1 hides the panel and
the help.

| Keyboard and mouse | Gamepad | What |
| --- | --- | --- |
| WASD or arrows | left stick | run, relative to the camera |
| Space (hold for height) | South (A) | jump; in the air: the extra jump, then the glide (hold) |
| Shift | right shoulder | sprint, only while "Sprint allowed" (4) is on |
| mouse (captured), Q/E | right stick | look |
| V | North (Y) | third person / first person |
| 1 | D-pad up | extra jump on/off |
| 2 | D-pad right | glide on/off |
| 3 | D-pad down | speed boost on/off |
| 4 | | sprint allowed on/off |
| T | D-pad left | teleport to the next station |
| R | Back | back to the start |
| L | | labels and floor grid on/off |
| P | | copy every value to the clipboard, and log it |
| 0 | | reset every value to its default |

The gamepad is the first one connected. The demo panel's Readings section says whether one is.

**The glide:** press jump in the air once any extra jumps are spent, and hold it. Kiki sinks at the glide's sink speed
until you let go or land. "Glide on held jump" also starts it whenever jump is held while falling, which is the other
common scheme to compare.

## Tuning and reading out values

Every number is a slider or switch on the demo panel (right). They are grouped as **Power-ups**, **Run**, **Jump** and
**Camera**, and take effect at once. **Readings** shows the following:

- **Ground**: the character body's ground state, and whether it touches a wall or a ceiling.
- **Speed across, speed up**: how fast Kiki actually moved (`state.movedVelocity`), in m/s.
- **Feet height**: in metres above the floor.
- **Air state**: rising jump, airborne or gliding, and the extra jumps left.
- **Last jump**: from the last ground before takeoff to landing. It gives the peak rise, the distance across, how far
  up or down Kiki landed, and the time in the air. Each jump of over 0.1 s is also logged, and its path is drawn in
  orange until the next one.
- **Derived jump**: rising and falling gravity, the launch speed, and the height of a tapped jump.
- **Camera**: the mode, yaw, pitch, and the third-person distance in use after any pull-in.

P (or "Copy values to clipboard") puts every value, and the derived ones, on the clipboard as `name = value` lines
and logs them, ready to paste into the ticket.

### How the jump is specified

The jump is set by **held height** and **time to top**:

- rising gravity = 2 × height / time²
- launch speed = 2 × height / time
- falling gravity = rising gravity × **fall gravity ×**
- a jump let go while rising has rising gravity × **let-go gravity ×**, which gives the tapped (low) jump

Coyote time and the jump buffer are in seconds. The extra jump is set by its own rise from wherever it starts. The
held jump peaks at its set height to within a centimetre, and the launch tick takes half a tick's gravity for that.

## Defaults

| Value | Default | Value | Default |
| --- | --- | --- | --- |
| Top speed | 7.0 m/s | Extra jump | off, 1, 1.5 m rise |
| Acceleration | 60 m/s² | Glide | off; sink 2.5 m/s, brake 40 m/s², speed ×1.15 |
| Stopping (and turning against the run) | 80 m/s² | Glide on held jump | off |
| Facing turn | 900°/s | Speed boost | off, ×1.5 |
| Sprint | not allowed, ×1.4 | Camera | third person |
| Held jump height | 2.0 m | Distance | 6.0 m |
| Time to top | 0.38 s | Pivot | 1.0 m above the capsule's centre (1.5 m above the feet) |
| → rising gravity, launch speed | 27.7 m/s², 10.5 m/s | Pitch limits | 70° down to 30° up, starting 20° down |
| Fall gravity × | 1.8 (49.9 m/s²) | Pull in against walls | on, 0.25 m sphere, closest 0.6 m, eases out at 6 m/s |
| Let-go gravity × | 3.0 (tapped jump about 0.7 m) | Follow sharpness | 20/s across, 8/s up and down |
| Max fall | 30 m/s | FOV | 60° third person, 75° first person (vertical) |
| Air control, air drag | 30 m/s², 8 m/s² | Eye height | 0.3 m above the centre (0.8 m above the feet) |
| Coyote time, jump buffer | 0.12 s each | Mouse, stick, Q/E | 0.12°/count, 200°/s, 120°/s; Y not inverted |

Measured on the flat floor at the defaults: a held jump rises 2.00 m and spends 0.68 s in the air. A one-tick tap
rises 0.73 m.

**Kiki** is a capsule 1.0 m tall and 0.6 m across (half-height 0.2 m, radius 0.3 m). Its character body walks steps
of up to 0.3 m, snaps to the floor 0.3 m down, and has the default 45° walkable slope. The brown box is its body. The
beak shows which way it faces, which follows its velocity at the facing turn speed.

## The course

Heights are of top surfaces above the floor (y = 0). Gaps are edge to edge at equal heights. A 1 m grid on the floor
(bolder every 5 m) and a label on each piece give the measurements in place. T steps through the stations.

| Station (T) | Where | What |
| --- | --- | --- |
| Start | origin, looking north (-Z) | a white pad |
| Ledges | west, front faces on z = -10 | 2.5 m wide, 3 m deep, 1.5 m apart: **0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0, 5.0, 6.0 m** high |
| Gap runway | east | a runway 1.0 m high (four 0.25 m steps up its west end), with ten landing pads north of its north edge, each 2.5 m wide and 4 m deep, 1.0 m high, across gaps of **2, 3, 4, 5, 6, 7, 8, 9, 10, 12 m** |
| Ramps | south-west | 3 m wide, rising south to a 2.5 m top with a 2 m landing: **15°, 30°, 40°, 45°** (the walkable limit) and **50°** (too steep) |
| Wall and corridor | south-east | a wall 20 m long and 8 m high, an inside corner, and a corridor 2 m wide between 4 m walls |
| Low ceilings | south | a tunnel 8 m wide with its ceiling at **1.4 m** (0.4 m over Kiki's crown), then one at **2.2 m** (under a full jump's top), then an overhead platform **3.5 m** up on its underside (4.0 m top) to walk under and look up at |
| Tower top | north | seven 3 m blocks stepping up by **1.5 m** each to **10.5 m**, with open floor north of it to glide over |

## What it showed about `CharacterBody`

Everything above is built on what `CharacterBody` gives today, with no engine change.

- **Its state is one tick old.** `state` describes the last move. On the first tick that reads "in air", Kiki has
  already moved one tick up. The jump record therefore measures from the last grounded position, not from the first
  airborne one.
- **The camera's pull-in is game code over `PhysicsWorld::ShapeCast`.** It uses two sphere casts that ignore Kiki: one
  up from the capsule to the pivot (under ceilings), and one back to the camera.
