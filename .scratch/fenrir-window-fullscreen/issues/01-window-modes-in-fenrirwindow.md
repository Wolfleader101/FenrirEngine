# 01: Window modes in FenrirWindow

Spec: [Borderless fullscreen](../spec.md).

**What to build:** A window has a window mode, windowed or borderless fullscreen. `WindowConfig` sets the main window's mode at creation (default windowed), and per-window calls set and read it while running. Setting borderless fullscreen remembers the windowed size and position and covers the window's current display at its desktop mode through the windowing library's desktop fullscreen; setting windowed restores them; setting the current mode does nothing. The size change sends the existing resize event, and a mode-changed event tells readers the new mode, including when the platform takes the window out of fullscreen. The renderer follows the size through its existing resize path.

**Blocked by:** None (can start immediately)
**Touches:** `packages/FenrirWindow/include/FenrirWindow/`, `packages/FenrirWindow/src/`, `packages/FenrirWindow/Tests/`, `packages/FenrirWindow/api/`, `packages/FenrirRenderer/Tests/`

**Status:** ready-for-agent

First; 02 and 03 build on it side by side.

- [ ] FenrirWindow display tests (tagged `[display]`, skipped without a display): a window made fullscreen covers its display; switching to fullscreen covers the display and sends a resize; switching back restores the windowed size and position and sends a resize.
- [ ] The mode reads back; setting the same mode twice does nothing; the mode-changed event is sent.
- [ ] The renderer's resize tests cover a mode change where they drive a real window.
- [ ] The API description is current.
