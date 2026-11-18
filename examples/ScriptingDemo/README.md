# ScriptingDemo

The scripting showcase, on the demo scaffold (`examples/DemoScaffold/README.md`, for the keys every demo app shares and
the smoke-run switches): the legacy DemoApp's scripting demo, ported unchanged. It has one scene, "Showcase", and exists
only with scripting built (`FENRIR_ENABLE_SCRIPTING`, on in the clang presets and `msvc-x64-debug`; needs the .NET 10
SDK).

```sh
cmake --build out/build/clang-x64-debug --target ScriptingDemo
cd out/build/clang-x64-debug/examples/ScriptingDemo
./ScriptingDemo.exe
FENRIR_DEMO_SMOKE=900 FENRIR_DEMO_BACKEND=null ./ScriptingDemo.exe
```

Building `ScriptingDemo` also builds the scripts project `scripts/ScriptingDemo.Scripts.csproj` into `scripts/` beside the
executable. The log shows `Scripting: loaded 'ScriptingDemo.Scripts.dll'.` and the scripts start working.

## What it shows

Each piece is a small, commented file in `scripts/` (the C# half) and, where C++ is involved, `src/Showcase.cpp` (the C++
half). The legacy editor's hierarchy is gone, so the names it showed are drawn as world text instead: each box's
above it, and each spark's where it is.

| You see | What it shows | Where |
|---|---|---|
| Both boxes turn about the Y axis. `R` reverses them, and `[` and `]` slow them down and speed them up. | A **component defined in C#** (`Spin`, nothing registered anywhere); a C# **system** that **queries** an engine component (`Transform`) together with it and writes the engine's memory directly. How they turn is one `SpinDial`, another C# component. | `Spin.cs`, `Controls.cs` |
| The demo panel's "Spin (Razor)" section: a Reversed toggle, a logarithmic Speed range and a readout of the sparks alive, styled like the C++ rows. | **A Razor panel in a demo slot**: `ShowcasePanel.razor`, built from the demo UI kit's shared Razor components (`examples/DemoScaffold/README.md`, "C# scenes"), mounted into the slot Showcase asks for (`razorPanel`) by a C# system, from `OnCreate` and again on each entry. It reads and writes the `SpinDial` through `Context`, so the panel and the keys agree. | `ShowcasePanel.razor`, `ShowcasePanelSetup.cs` |
| Box 2 bobs up and down, and its name above it keeps changing (`Box 2 (bob phase 3.75)`). | **A component shared between C++ and C#**: `Bob` is declared in C++, mirrored in C# with `[NativeComponent]`, and checked field by field at load. C++ sets how it looks, C# animates it and writes `Phase`, and a C++ system reads `Phase` back into the entity's `Name`. One copy of the data, nothing converted. | `Bobbing.cs`, `Showcase.cpp` |
| `R: reversed the spin.` in the log when you press R. | **An event from C++ to C#**: FenrirInput's C++ `KeyEvent`, read from C# unchanged (`Fenrir.Input.KeyEvent`). | `Controls.cs` |
| Press `Space`: five `Spark N` names appear in a row above the boxes and vanish a couple of seconds later. | **Creating and destroying entities from C#**, giving them components and names, and **changes landing between systems** (the `Dresser` sees the sparks the `Spawner` made a moment before, the `Reaper`'s destroys never disturb its own loop). | `Sparks.cs` |
| `C++ saw a burst of 5 sparks sent from C#.` | **An event from C# to C++**: `Burst` is one queue read by both languages. | `Controls.cs`, `Sparks.cs`, `Showcase.cpp` |
| `Spark 3 burnt out.` | **An event only C# defines** (`Expired`): no C++ type and no registration; the build finds it and the engine makes its queue. | `Sparks.cs` |
| Press `G`: `G: GPU culling off; 2 of 2 instances drawn.` in the log a few frames later (`0 of 2` on the null backend, which runs no cull shader); `G` again turns it back on. | **The renderer's switches and statistics from C#**: `Renderer.SetGpuCulling` turns the GPU frustum test (ADR-0057) off or on, and `Renderer.Stats` reads what the cull passes drew, through the `Fenrir.Renderer` contract. | `Culling.cs` |
| Edit a script while the demo runs (below). | **Hot reload**, with the data kept: the spin angles carry on. | all of them |

Each system reads events through readers of its own (`EventReader<T>` fields in C#, captured in the lambda in C++), so it
handles each event once, whichever language sent it. `Bob` and `Burst` are the app's own scripting extension
(`ScriptingDemo::Extension()`, given to the scaffold as `DemoConfig::scriptingExtensions`); the boxes are the shared
`BoxTextured/BoxTextured.gltf` (Khronos's textured box), spawned with `SpawnModel` and drawn by `MeshRenderer`.

## The demo panel

Showcase's switches are on the demo panel (the demo UI kit's, `examples/DemoScaffold/README.md`), which F1 hides and
shows. Under the scene list and Showcase's own section (its Tutorial button) is Showcase's demo slot (`razorPanel`),
which holds the "Spin (Razor)" section: `ShowcasePanel.razor`, a Razor component in the scripts, mounted there by
`ShowcasePanelSetup.cs`. It is built from the kit's shared Razor components and looks like the C++ rows.

| Row | Does | Key |
|---|---|---|
| Reversed (toggle) | Turns the boxes the other way. | `R` |
| Speed (logarithmic range, x0.10 to x10) | How fast the boxes turn. | `[` / `]` |
| Sparks alive (readout) | How many sparks are alive. | `Space` makes five |

The panel and the keys write the same `SpinDial` (`Spin.cs`), so each shows what the other did. Each switch also has
its key, so the help lists every one (ADR-0072). ScriptingDemo is only built with scripting on, so its slot holds the
panel; a slot with no script to mount (another demo's, built without scripting) shows a note that the panel needs
scripting and that its keys are in the help.

The help (F1 shows it with the panel) lists the scene's key rows, after the scaffold's keys
(`examples/DemoScaffold/README.md`, "Keys"):

| Key | Does |
|---|---|
| `R` | Reverses the boxes' spin (the panel's Reversed toggle). |
| `[` / `]` | Slows the spin down or speeds it up, by half again each press, from x0.10 to x10 (the panel's Speed range). |
| `Space` | A burst of five sparks. |
| `G` | Turns the renderer's GPU culling off, or back on, and logs what the cull passes drew. |

The first time Showcase is entered in a run, a tutorial of four steps shows at the top centre: the scene list, F1, the
C# systems' keys (it moves on when R is pressed), and the panel being a Razor component that comes back when the
scripts are rebuilt. The Tutorial button in the panel's Showcase section shows it again. Smoke runs and
`FENRIR_DEMO_KEYS` runs never show it.

### Change the script while the demo runs

Leave the demo running, edit `scripts/Spin.cs` (change a speed, the log message), and rebuild only the scripts:

```sh
cmake --build out/build/clang-x64-debug --target ScriptingDemoDeployScripts
```

The demo notices the new build, reloads it between frames and logs `Scripting: reloaded 'ScriptingDemo.Scripts.dll'`. The
new code runs without a restart, and each box's `Angle` (kept in the `Spin` component) carries on from where it was.
See "Hot reload" in `packages/FenrirScripting/README.md`. Smoke runs turn hot reload off.

## Smoke runs

CTest runs `ScriptingDemo-Smoke-null` and `ScriptingDemo-Smoke-opengl`, as for every demo app, and
`ScriptingDemo-Showcase-null` and `ScriptingDemo-Showcase-opengl`: 900 frames of Showcase with R, Space, G and ]
pressed, which pass only if the log has the lines above (the spin, `Bob.Phase` read by C++, the key event, the burst, a
burnt-out spark, the Razor panel mounted in its slot, `]: the spin's speed is x1.50.` and the culling switch with its
count; `cmake/CheckShowcaseLog.cmake`).

## Hand checks

On OpenGL:

- The window shows Fenrir's icon (`examples/assets/icons/fenrir.png`, `DemoConfig::iconPath`) in its title bar and on
  its taskbar button, not the system's default.
- ScriptingDemo opens a window on Showcase: the two textured, lit boxes, one above the other, turning about the Y
  axis in opposite directions; the log says `Spinning 2 entities from C#. Press R to reverse them, Space for sparks.`
- The tutorial card shows at the top centre, titled Showcase, at `1/4` "The scenes". Next steps it through "The
  demo UI" and "The C# systems", whose key rows list R, `[ / ]`, Space and G; pressing R there moves it on to "A Razor
  panel", and Done closes it. PageDown, which enters Showcase again, does not show it again; the Tutorial button in
  the panel's Showcase section shows it again from step one.
- The help lists the key rows R, `[ / ]`, Space and G under the scaffold's keys. F1 hides the panel and the help, and
  F1 again shows them and logs `Help: ...` lines.
- The upper box bobs up and down, and the name above it, `Box 2 (bob phase …)`, counts up as it does; the log
  says `C++ read Bob.Phase = …, which C# wrote into the same component.` once.
- R reverses both spins, and the log says `R: reversed the spin.`; R again turns them back.
- The demo panel shows a "Spin (Razor)" section after the Showcase section, laid out and styled like the C++
  sections: a Reversed checkbox, a Speed slider with `1.00` beside it, and Sparks alive `0`; the log says
  `ScriptingDemo: the Razor panel is in Showcase's demo slot`. Its heading collapses and opens it.
- Clicking Reversed reverses the boxes, and R ticks and unticks it. Dragging Speed changes how fast they turn (the
  slider is logarithmic: its middle is x1), and `[` and `]` move the slider and its value. Hovering a row shows its hint
  at the panel's foot.
- Space makes Sparks alive count up to 5 and back down to 0 as they burn out.
- Space puts five `Spark N` names in a row above the boxes, logs `C++ saw a burst of 5 sparks sent from C#.`, and
  over the next two to four seconds each name vanishes as `Spark N burnt out.` is logged. Several presses number the
  sparks on (`Spark 6`, `Spark 7`…).
- G logs `G: GPU culling off; 2 of 2 instances drawn.` a few frames later, and the boxes look as they did (culling
  off draws everything, so nothing changes on screen); G again logs `G: GPU culling on; 2 of 2 instances drawn.`.
- Hot reload: with the demo running, change `RadiansPerSecond` in `scripts/Spin.cs`'s `SpinSetup` to `3.0f` and the log
  message, and build `ScriptingDemoDeployScripts`. The log says `Scripting: reloaded 'ScriptingDemo.Scripts.dll'` and
  `ScriptingDemo: the Razor panel is in Showcase's demo slot` again, the "Spin (Razor)" section is back in the panel
  and still works (Reversed, Speed, the sparks' readout), and the boxes keep turning from where they were, without a
  jump. PageDown (which enters Showcase again) spawns them turning at the new speed with the new message, with the
  panel in its slot.
