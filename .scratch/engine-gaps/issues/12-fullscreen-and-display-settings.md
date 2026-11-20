# No fullscreen or display settings

Status: ready-for-agent
Blocked by: none

## What is missing

FenrirWindow's public API has no fullscreen or borderless mode, no way to change resolution, and no list of displays or modes. MSAA is fixed when the renderer is installed.

## Who needs it

Flightless Frenzy ([map](../../flightless-frenzy/map.md)): a game is played fullscreen, and its options screen would offer fullscreen or windowed at least.

## Does anything plan it?

No. Several windows are planned on the renderer map (`.scratch/fenrir-renderer/map.md:85`), which is a different thing.

## Does it block the game?

No. The game runs in a window until this exists.

## Comments

- 2026-11-19, charting [Engine gaps for Flightless Frenzy](../map.md): settled by [What do fullscreen and display settings look like?](31-what-fullscreen-and-display-settings-are.md).
- 2026-11-19, round 1 of charting [Engine gaps for Flightless Frenzy](../map.md): windowed and borderless fullscreen are spec-ready, exclusive modes deferred, in [What do fullscreen and display settings look like?](31-what-fullscreen-and-display-settings-are.md).
- 2026-11-20: the spec is written, [Borderless fullscreen](../../fenrir-window-fullscreen/spec.md), `ready-for-agent` for `/to-tickets`.
- 2026-11-20: tickets cut, `.scratch/fenrir-window-fullscreen/issues/01` to `03` (01 first, then 02 and 03 side by side).
