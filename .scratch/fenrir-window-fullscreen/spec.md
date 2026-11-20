# Borderless fullscreen

Status: ready-for-agent

Charted by [Engine gaps for Flightless Frenzy](../engine-gaps/map.md): [What do fullscreen and display settings look like?](../engine-gaps/issues/31-what-fullscreen-and-display-settings-are.md). Settles the first half of [No fullscreen or display settings](../engine-gaps/issues/12-fullscreen-and-display-settings.md); exclusive fullscreen and display lists are deferred there.

## Problem Statement

FenrirWindow's main window is always a resizable window of the size its config gives. There is no fullscreen of any kind, at creation or while running, from C++ or C#. A game is usually played fullscreen, and Flightless Frenzy's options screen is to offer a fullscreen toggle once the engine can do it.

## Solution

A window has a **window mode**: windowed or borderless fullscreen. `WindowConfig` sets the main window's mode at creation, and the window's mode can be changed at any time from C++ and from C# (`Window.Fullscreen`, a bool over the mode). Borderless fullscreen covers the display the window is on at that display's desktop mode, changing no display mode; leaving it restores the window's last windowed size and position. The renderer follows the size change as it follows a resize today. Every demo app toggles it with F11.

## User Stories

1. As a game developer, I want my game to start in borderless fullscreen, so that it opens the way a game should.
2. As a game developer, I want to switch between windowed and borderless fullscreen while running, so that my options screen can offer it.
3. As a game developer, I want borderless fullscreen to cover the display the window is on, so that a player with two monitors gets the one they chose.
4. As a game developer, I want borderless fullscreen to leave the display's mode alone, so that switching is instant and alt-tab is safe.
5. As a game developer, I want leaving fullscreen to restore the window's size and position, so that the player's window comes back where it was.
6. As a game developer, I want to read the current mode, so that my options screen shows the right state.
7. As a game developer, I want a resize event when the mode changes the window's size, so that my UI lays out again.
8. As a game developer, I want the renderer to follow the new size with nothing from me, so that the frame fills the screen.
9. As a game developer, I want the mode to be my choice, not changed behind my back, so that the player's setting holds.
10. As a C# game developer, I want `Window.Fullscreen` and the mode from C#, so that my Razor options screen toggles it.
11. As an engine maintainer, I want display tests of the mode changes, so that a regression shows on a machine with a display.
12. As an engine maintainer, I want F11 to toggle fullscreen in every demo app, so that I can check it by hand anywhere.

## Implementation Decisions

- **The window mode** is an enum, windowed or borderless fullscreen, in FenrirWindow's types. `WindowConfig` gains the main window's mode, default windowed.
- **The window** gains a call to set its mode and one to read it, per window id as its other calls are. Setting borderless fullscreen remembers the windowed size and position and covers the window's current display at its desktop mode through the windowing library's desktop fullscreen; setting windowed restores what was remembered. Setting the mode it is already in does nothing.
- **Events:** the size change sends the window's existing resize event; a mode-changed event tells readers the new mode, including when the player leaves fullscreen through the platform (where the platform allows it).
- **The renderer** follows the size change through its existing resize path; no renderer change is expected.
- **C#:** `Window.Fullscreen` (a bool: true is borderless fullscreen) and the mode itself, through FenrirWindow's scripting extension and the generated bindings.
- **The demo scaffold** gains a shared F11 key that toggles the main window between windowed and borderless fullscreen, listed in its help with the scaffold's others; no smoke run presses it.
- **Glossary:** Borderless fullscreen is already defined; *Window mode* is added: whether a window is windowed or borderless fullscreen.
- **No ADR:** it adds a mode the windowing library already offers and reverses nothing.

## Testing Decisions

- **A good test here sets a mode and checks the window's size, position, mode and events**, never the windowing library's calls.
- **FenrirWindow's display tests** (prior art: the window tests tagged `[display]`, skipped without a display): a window made in borderless fullscreen covers its display; switching to it at run time covers the display and sends a resize; switching back restores the windowed size and position and sends a resize; the mode reads back; setting the same mode twice does nothing.
- **The renderer's resize tests:** the view follows the window's new size on the null and GL backends (existing tests, extended with a mode change where they drive a real window).
- **FenrirWindowScripting's fixture tests:** `Window.Fullscreen` written from C# changes the mode and reads back.
- **By hand:** F11 toggles borderless fullscreen in every demo app and back to the same window; the smoke runs pass.

## Out of Scope

- Exclusive fullscreen with a chosen display mode, and listing displays and their modes (deferred on the map).
- Moving a window between displays.
- Changing MSAA or other renderer settings while running.
- Remembering the mode between runs (the game's settings file).

## Further Notes

- The facts behind the decisions are in [What do fullscreen and display settings look like?](../engine-gaps/issues/31-what-fullscreen-and-display-settings-are.md).
- Build and test only `msvc-x64-debug` locally, run the display tests on your own machine, and use the short-path junction in a worktree (`README.md`, "Building and testing").
