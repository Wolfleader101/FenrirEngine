# DemoApp

A windowed sample app: a renderer, an editor, two backpacks and, with physics on, physics and joints scenes (some built in C++,
some only in C#). It is also the smoke test of C# scripting in a real app (the proof of the scripting package is its headless tests, in
`packages/FenrirScripting/Tests`).

## Scenes and controls

The demo has up to four scenes, and the editor shows whichever is active. Each feature family gets one F-key, which steps through
the family's scenes and back to Default (`src/DemoScenes.hpp`); another family's key jumps to its first scene. `F3` is physics from
C#, `F4` joints; `F5` to `F8` are kept for the character controller, vehicles, ragdolls and soft bodies. Entering a scene logs its
keys.

- **Default**, the one it starts in: two backpacks, four lights and, with physics on, the C++ physics demo (a stack of boxes on a
  ground, a kinematic paddle sweeping through them, and a backpack with a triangle-mesh collider with boxes dropped onto it).
- **Physics (C#)**, with physics and scripting both on: a scene C++ creates empty but for one marker entity, built and run
  entirely by a C# script (see "The C# physics scene" below).
- **Joints (C++)**, with physics on: a hinged door and a hanging chain, built in C++ (`src/JointsScene.hpp`, whose header lists its
  keys).
- **Joints (C#)**, with physics and scripting both on: a breakable bridge, a lift, a spring rope and a limb, built and run entirely
  by a C# script (see "The C# joints scene" below).

| Key or button | Does |
|---|---|
| Right mouse (hold) and move | Looks around. |
| `W` `A` `S` `D`, `Shift` | Moves the camera; `Shift` is faster. The scroll wheel zooms. `F1` turns the camera controls off and on. |
| Left click in the scene view | Selects the entity under the cursor in the editor. In the Physics (C#) scene it also casts a ray from C#. |
| `F2` | Cycles the physics debug lines: off, colliders, contacts, velocities, bounds, joints, all (physics on). The mode is the app's, so it carries across a scene switch. |
| `F3` | Switches between Default and Physics (C#) (physics and scripting on; without scripting it does nothing). |
| `R`, `Space` | The scripting demo: reverse the backpacks' spin, make sparks (below). |
| `F4` | Steps Default → Joints (C++) → Joints (C#) → Default (physics on; without scripting it switches between Default and Joints (C++)). |
| `J` | In the Physics (C#) scene: kicks every box and sphere into the air. |

Physics steps only the active scene, so the scene you switch away from waits, bodies mid-air and all, and carries on from there
when you switch back. The scripting demo's C# systems also act only on the active scene, and each scene's keys act only while it
is active.

## With scripting (C#)

Needs the .NET SDK (the one Iris finds; .NET 10). Scripting is on in the clang presets (through `windows-clang-base`) and in
`msvc-x64-debug`: they set `FENRIR_ENABLE_SCRIPTING=ON`. Any other configuration defaults it to OFF.

```bash
cmake --preset clang-x64-debug
cmake --build out/build/clang-x64-debug --target DemoApp
```

Building `DemoApp` also builds the script project `scripts/DemoScripts.csproj` into `<exe dir>/scripts/`, which is where
`assets/demoApp.feproj` (`"scriptAssembly": "scripts/DemoScripts.dll"`) points. Run the demo from its own folder, as an IDE's run
button does:

```bash
cd out/build/clang-x64-debug/examples/DemoApp
./DemoApp.exe
```

The log shows `Scripting: loaded 'DemoScripts.dll'.` and the scripts start working.

### What the demo shows

Each piece is a small, commented file in `scripts/` (the C# half) and, where C++ is involved, `src/ScriptingDemo.hpp` (the C++ half).

| You see | What it shows | Where |
|---|---|---|
| Both backpacks turn about the Y axis. `R` reverses them. | A **component defined in C#** (`Spin`, nothing registered anywhere); a C# **system** that **queries** an engine component (`Transform`) together with it and writes the engine's memory directly. | `Spin.cs` |
| Backpack 2 bobs up and down, and its name in the editor's hierarchy keeps changing (`Backpack 2 (bob phase 3.75)`). | **A component shared between C++ and C#**: `Bob` is declared in C++, mirrored in C# with `[NativeComponent]`, and checked field by field at load. C++ sets how it looks, C# animates it and writes `Phase`, and a C++ system reads `Phase` back into the entity's `Name`. One copy of the data, nothing converted. | `Bobbing.cs`, `ScriptingDemo.hpp` |
| `R: reversed the spin.` in the log when you press R. | **An event from C++ to C#**: the window's existing C++ `KeyboardKeyEvent`, read from C# unchanged. | `Controls.cs` |
| Press `Space`: five `Spark N` entities appear in the hierarchy and vanish a couple of seconds later. | **Creating and destroying entities from C#**, giving them components and names, and **changes landing between systems** (the `Dresser` sees the sparks the `Spawner` made a moment before, the `Reaper`'s destroys never disturb its own loop). | `Sparks.cs` |
| `C++ saw a burst of 5 sparks sent from C#.` | **An event from C# to C++**: `Burst` is one queue read by both languages. | `Controls.cs`, `Sparks.cs`, `ScriptingDemo.hpp` |
| `Spark 3 burnt out.` | **An event only C# defines** (`Expired`): no C++ type and no registration; the build finds it and the engine makes its queue. | `Sparks.cs` |
| Edit a script while the demo runs (below). | **Hot reload**, with the data kept: the spin angles carry on. | all of them |
| `F3`: a second scene of boxes, spheres, a paddle and a green trigger pad, all made by C#. | **Physics from C# only**, through the `Fenrir.Physics` contract (below). | `Physics/Playground.cs` |

Each system reads events through readers of its own (`EventReader<T>` fields in C#, captured in the lambda in C++), so it handles each
event once, whichever language sent it.

### The C# physics scene

`F3` switches to the "Physics (C#)" scene. All C++ does for it (`src/CSharpScenes.hpp`) is create the scene with one entity
carrying a `CSharpScene` marker, whose `Scene` id says which C# script builds there, and register it with the scene switcher on
`F3`; it creates no bodies. Everything else is `scripts/Physics/Playground.cs`:

| You see | What it shows |
|---|---|
| The first time you switch: a grey ground, a pyramid of ten boxes and five spheres of different sizes falling onto it, each in its own colour, an orange paddle and a green pad on the ground. The log says `Physics (C#): C# built ...`. | **Bodies made from C#**: entities given `RigidBody`, `Collider` (box and sphere shapes), `PhysicsMaterial` (the bouncy spheres), `ContactEvents`, `Trigger` and `CollisionFilter`, all mirrored from FenrirPhysics. **Visible without the renderer**: each also gets a `DemoMesh` (cube or sphere, and a colour), a plain-data component the app shares with C# (`src/DemoMesh.hpp`, `scripts/DemoMesh.cs`), which a C++ system turns into the renderer's `Model` and `Material`. |
| The paddle sweeps from side to side through the pyramid, pushing the boxes. | **A kinematic body moved by a C# `Tick` system**, writing its `Transform`. The sweep's progress is kept in a C#-defined component, so it pauses with the scene and survives a hot reload. |
| A box or sphere on the green pad turns pale while it is there; the log says `TriggerEnter: Box 3 entered the Trigger zone.` and `TriggerExit: ...`. | **Trigger events read from C#** through `EventReader<TriggerEnter>` and `EventReader<TriggerExit>` fields, and writing `DemoMesh.Colour` to recolour the body. The trigger's `CollisionFilter` leaves out the ground and the paddle. |
| `ContactBegin: Ground and Sphere 4 met at 11.0 m/s, ...` when a sphere lands hard. | **Contact events read from C#** (`EventReader<ContactBegin>`), on the spheres, which carry `ContactEvents`. Only hits faster than 3 m/s are logged. |
| `J` throws every box and sphere into the air. | **`PhysicsWorld.AddImpulse`** from C#, on the `KeyboardKey` event. |
| A left click logs `Raycast: hit Box 6 at (...), 11.7 m away, ...` and pokes what it hit. | **`PhysicsWorld.Raycast`** from C#, along the ray the editor sends for a click in the scene view (`SceneClickEvent`, shared as `Demo.SceneClick`). |

`F3` again goes back to Default, and the C# scene's world waits until you return. The scripts in `scripts/Physics/` are built
against the `Fenrir.Physics` contract only when physics is on; with physics off they are left out of `DemoScripts`.

### The C# joints scene

The second `F4` press switches to "Joints (C#)". As for the physics scene, C++ makes it empty but for a `CSharpScene` marker and
registers it; everything else is `scripts/Physics/Joints.cs`, built in one system from the `Fenrir.Physics` joints. It builds over
two frames: the bodies and their joint entities first (the joints linked to bodies made earlier in the same call), and then, once
the bodies' Transforms have landed, each joint's frames from `Joint.FrameFromWorld`.

| You see | What it shows |
|---|---|
| A bridge of four planks sagging between two posts, each joint's name showing its angle and load (`Bridge middle: 0.00 rad, 55 N`). | **Hinge joints** linked to entities made in the same system, frames from **`Joint.FrameFromWorld`**, and **`PhysicsWorld.GetJointState`** read every frame into each joint's `Name`. |
| `B` drops a weight; the bridge's middle hinge breaks, and the log says `JointBroken: Bridge middle broke at ...`. `R` repairs it. | **Breaking**: a `BreakForce`, the break read through `EventReader<JointBroken>`, and repairing by writing `Enabled = true`. The weight is made at runtime with `Entity.Create`. |
| `K` makes the bridge's middle joint fixed, so the middle planks are one beam, and a hinge again. | **Swapping a kind by assigning it** (`joint.Kind = new FixedJoint()`). |
| A platform beside a column; `1` drives it up to its top limit, down to its bottom one, to half way, and off. | A **slider** with limits, its motor edited through its view (`joint.Kind.Slider.Motor`). |
| A ball bobbing on a rope from a hook in mid-air. | A **distance joint** with a soft spring, to a world point, frames from `FrameFromWorld`'s world overload. |
| An arm hanging from a shoulder, swinging up against its cone once at the start; `2` raises it and lowers it. | A **swing-twist joint** with cone limits and an orientation motor, its target edited through its view. |
| `0` logs every joint's full state. | `GetJointState`: force, torque, frameB's pose relative to frameA, and the position. |

With the joints debug lines on (`F2`), each joint's two frames coincide at rest; the rope's are apart by its length.

### Running it without a keyboard

Set `FENRIR_DEMO_AUTOPLAY=1` and the demo presses `R` after a couple of seconds and `Space` after a couple more, by sending the same
`KeyboardKeyEvent` the window would. With physics on it then visits the C# physics scene: `F3`, a click at the box pyramid (the
editor's `SceneClickEvent`), `J`, `F3` back and `F3` again, about two seconds apart. It is how the showcase was checked end to end:

```bash
cd out/build/clang-x64-debug/examples/DemoApp
FENRIR_DEMO_AUTOPLAY=1 ./DemoApp.exe
```

To run one scene by itself, `FENRIR_DEMO_SCENE=<name>` starts in it (by its name, such as `"Physics (C#)"`), and with
`FENRIR_DEMO_KEYS=1` the scene switcher presses that scene's keys, two seconds after it becomes active and then one a second, as
`KeyboardKeyEvent`s, so C++ and C# both read them:

```bash
FENRIR_DEMO_SCENE="Physics (C#)" FENRIR_DEMO_KEYS=1 ./DemoApp.exe
```

The joints scenes are `"Joints (C++)"` and `"Joints (C#)"`.

### Change the script while the demo runs

Leave the demo running, edit `scripts/Spin.cs` (change a speed, the log message), and rebuild only the scripts:

```bash
cmake --build out/build/clang-x64-debug --target DemoScripts
```

The demo notices the new build, reloads it between frames and logs `Scripting: reloaded 'DemoScripts.dll'`. The new code runs without
a restart, and each backpack's `Angle` (kept in the `Spin` component) carries on from where it was. See "Hot reload" in
`packages/FenrirScripting/README.md`.

## Without scripting

Configure with `-DFENRIR_ENABLE_SCRIPTING=OFF`. The demo builds and runs as it always did: no script project is built, no
`scripts` folder is made, and `dotnet` is never looked for or started.

## Editing the C# in VS Code

The script projects open without anything built: `FenrirScripting.sln` (at the repository root) holds the contract, the source
generator, `DemoScripts` and the test fixture. Point the C# extension at it with the setting
`"dotnet.defaultSolution": "FenrirScripting.sln"` (`.vscode/` is not committed, so this is yours to set), which also keeps the extension
from loading the unrelated legacy `.csproj` files under `libs/`. Completion, go-to-definition and the generated `Ref<T>` accessors (`transform.Position.Y`) all work, because a script
project in this repository imports `packages/FenrirScripting/Managed/Fenrir.Scripting.InRepo.props`, which references the contract and
generator sources when CMake has not supplied a built SDK folder. A script project outside the repository imports the SDK folder's
`Fenrir.Scripting.props` instead (see `packages/FenrirScripting/README.md`).
