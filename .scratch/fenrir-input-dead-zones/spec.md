# Stick and trigger dead zones

Status: ready-for-agent

Charted by [Engine gaps for Flightless Frenzy](../engine-gaps/map.md): [Does the engine offer input actions or dead zones?](../engine-gaps/issues/29-does-the-engine-offer-input-actions.md). Settles the dead-zone half of [No input actions or bindings](../engine-gaps/issues/09-input-actions.md); the action layer is deferred there.

## Problem Statement

FenrirInput reports a gamepad's axes as the windowing library gives them, clamped to their range and nothing more. A stick at rest rarely reads zero, so every game that reads a stick must write its own dead zone, and each writes it differently: PhysicsDemo's vehicle scenes, in C++ and in C#, each carry a 0.15 dead zone of their own. A per-axis dead zone also bends a stick's direction near its axes. Flightless Frenzy wants a gamepad with sensible stick dead zones from the engine.

## Solution

FenrirInput applies a dead zone to every gamepad's sticks and triggers before any reader sees them. A stick's dead zone is radial: inside its inner value the stick reads zero, and beyond it the length is rescaled so it still runs smoothly to full, keeping the direction; an outer value lets a worn stick reach full. Triggers get a threshold the same way. Each stick and the triggers have their own values with engine defaults, settable per gamepad from C++ and C#. The `Gamepads` resource and axis events report dead-zoned values, and raw values stay readable for a game that shapes them itself. PhysicsDemo's vehicles drop their own dead zone.

## User Stories

1. As a game developer, I want a resting stick to read zero, so that my character does not drift.
2. As a game developer, I want the dead zone applied before I read a stick, so that I never write one myself.
3. As a game developer, I want a radial dead zone on each stick, so that pushing a stick diagonally keeps its direction.
4. As a game developer, I want the value rescaled past the dead zone, so that movement starts smoothly from zero instead of jumping.
5. As a game developer, I want an outer value, so that a worn stick still reaches full tilt.
6. As a game developer, I want a threshold on each trigger, so that a trigger at rest reads zero and a light squeeze starts from zero.
7. As a game developer, I want the left and right sticks and the triggers to have their own values, so that aiming can be finer than moving.
8. As a game developer, I want sensible engine defaults, so that most games set nothing.
9. As a game developer, I want to set the values per gamepad, so that a player's worn pad can be tuned in an options screen.
10. As a game developer, I want axis events to report dead-zoned values, so that events and reads agree.
11. As a game developer, I want no axis events for jitter inside the dead zone, so that a resting stick is quiet.
12. As a game developer, I want the raw values still readable, so that I can apply my own shaping when I need it.
13. As a game developer, I want UI navigation to read the dead-zoned stick, so that menus ignore a resting stick too.
14. As a C# game developer, I want to set a gamepad's dead zones and read dead-zoned and raw values from C#, so that my options screen is C#.
15. As an engine maintainer, I want PhysicsDemo's vehicles to use the engine's dead zone, so that the demos show it and lose their stopgap.
16. As an engine maintainer, I want the dead zone tested through the input feed, so that it holds for every source of input.

## Implementation Decisions

- **Where:** FenrirInput applies dead zones as axis values come through the input feed, so every source (the windowing library, tests, headless apps) gets them; FenrirWindow keeps clamping and nothing more.
- **A stick** is the pair of its axes. With raw (x, y) of length `l`, inner `i` and outer `o`: below `i` it reads (0, 0); otherwise its length becomes `min((l - i) / (o - i), 1)` along the raw direction. Inner defaults to 0.15 and outer to 1 as starting values, tuned in the spec's ticket.
- **A trigger** with threshold `t` and outer `o` reads 0 below `t` and `min((v - t) / (o - t), 1)` above it; threshold defaults to 0.05, outer to 1.
- **Settings** are per gamepad: left stick, right stick and triggers each with their values, starting from engine defaults that an app can change for every pad, and changeable for one pad by its id. They live with the gamepad's state and go when it disconnects.
- **What readers see:** `Gamepads::Axis` returns the dead-zoned value; a raw read returns what arrived. `GamepadAxisEvent` carries the dead-zoned value and the raw value, and is sent only when the dead-zoned value changes, so jitter inside a dead zone sends nothing. Since a stick is one pair, moving one axis can change the other's dead-zoned value, which sends events for both.
- **FenrirUI** keeps `UIRoot::NavigationDeadZone`, a direction threshold, applied to the dead-zoned stick.
- **C#:** the settings and the raw read, through FenrirInput's scripting extension and the generated bindings.
- **PhysicsDemo's vehicle scenes**, C++ and C#, drop their own dead zones and read the engine's values.
- **Glossary:** *Dead zone* is added by the first ticket; a trigger's dead zone is called its threshold, and `UIRoot::NavigationDeadZone` stays a navigation threshold.
- **No ADR:** it is a building block within FenrirInput that reverses nothing.

## Testing Decisions

- **A good test here feeds raw axes and checks what readers see**, never how the maths is coded.
- **FenrirInput's gamepad tests** (prior art: the gamepad input tests through the input feed): a resting stick inside the inner value reads zero; a diagonal push keeps its direction; values past the inner value rescale continuously from zero; the outer value reaches full early; trigger thresholds; per-stick and per-pad settings; defaults for a new pad; raw reads unchanged; events carry both values and none are sent for jitter inside the dead zone; settings go with a disconnected pad.
- **FenrirUI's navigation tests:** a stick resting inside the dead zone does not move focus; navigation past its own threshold still works.
- **FenrirInputScripting's fixture tests:** settings written from C# reach the pad; dead-zoned and raw values read in C#.
- **By hand, PhysicsDemo's vehicle scenes** drive from a gamepad with the engine's dead zone and no code of their own; the demo's smoke runs pass.

## Out of Scope

- An action layer, bindings and rebinding (deferred on the map).
- Response curves beyond the linear rescale.
- Per-axis (square) dead zones.
- Rumble and gyro.

## Further Notes

- The facts behind the decisions are in [Does the engine offer input actions or dead zones?](../engine-gaps/issues/29-does-the-engine-offer-input-actions.md).
- Build and test only `msvc-x64-debug` locally, and use the short-path junction in a worktree (`README.md`, "Building and testing").
