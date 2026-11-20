# What do fullscreen and display settings look like?

Type: grilling
Status: resolved
Blocked by: none
Settles: [No fullscreen or display settings](12-fullscreen-and-display-settings.md)

## Question

FenrirWindow has no fullscreen or borderless mode, no resolution change and no list of displays or modes; MSAA is fixed when the renderer is installed. Flightless Frenzy's options screen adds a fullscreen toggle once the engine can do it.

- Which modes: windowed, borderless fullscreen, exclusive fullscreen with a chosen mode; which ship first.
- Displays and modes: listed or not in the first version.
- MSAA and other renderer settings changed at run time: in this effort or left.
- The C# surface.

Outcome: a spec-ready design.

## Facts (checked 2026-11-19)

- FenrirWindow's API (`packages/FenrirWindow/include/FenrirWindow/Window.hpp:51-157`) has size, scale, title, icon, cursor, clipboard, `RequestClose`, surface and swap interval; no fullscreen. SDL3 backend; windows are created resizable and high-DPI (`packages/FenrirWindow/src/Window.cpp:222`); `SDL_SetWindowFullscreen` is never called.
- MSAA is `RendererConfig::msaa` (1, 2, 4 or 8), fixed at install (`packages/FenrirRenderer/include/FenrirRenderer/Renderer.hpp:64`).

## Answer

Grilled 2026-11-19, round 1 of charting [Engine gaps for Flightless Frenzy](../map.md); the recommendation was accepted.

**Windowed and borderless fullscreen; spec-ready.**

- FenrirWindow gains a window mode: **windowed** or **borderless fullscreen**, covering the display the window is on at that display's desktop mode. Set at creation and changed at run time, from C++ and from C# (`Window.Fullscreen`, a bool, over the mode).
- Leaving fullscreen restores the window's last windowed size and position.
- The renderer follows the size change as it does a resize today; nothing else changes.
- **Deferred:** exclusive fullscreen with a chosen display mode, and listing displays and their modes; they come back when a game's options want a resolution list.
- **Out:** changing MSAA or other renderer settings while running; MSAA stays fixed at install.
